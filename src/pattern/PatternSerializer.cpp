#include "pattern/PatternSerializer.h"

#include <limits>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace geck::pattern {

namespace {

    constexpr qint64 INT32_LO = std::numeric_limits<int>::min();
    constexpr qint64 INT32_HI = std::numeric_limits<int>::max();
    constexpr qint64 U32_HI = std::numeric_limits<uint32_t>::max();
    constexpr qint64 U16_HI = std::numeric_limits<uint16_t>::max();

    QJsonObject objectToJson(const PatternObject& obj) {
        QJsonObject json;
        json["dxHex"] = obj.dxHex;
        json["dyHex"] = obj.dyHex;
        json["proPid"] = static_cast<qint64>(obj.proPid);
        json["frmPid"] = static_cast<qint64>(obj.frmPid);
        json["direction"] = static_cast<qint64>(obj.direction);
        json["flags"] = static_cast<qint64>(obj.flags);
        return json;
    }

    QJsonObject tileToJson(const PatternTile& tile) {
        QJsonObject json;
        json["dxTile"] = tile.dxTile;
        json["dyTile"] = tile.dyTile;
        json["tileId"] = static_cast<qint64>(tile.tileId);
        return json;
    }

    // Validates a JSON value as an integer within [lo, hi]. Rejects non-numeric values
    // and out-of-range integers (which would otherwise wrap when narrowed to the target
    // type), so engine IDs/directions/flags/tile-ids cannot be silently corrupted.
    bool checkInt(const QJsonValue& value, qint64 lo, qint64 hi, const QString& key, qint64& out, QString* error) {
        if (!value.isDouble()) {
            if (error) {
                *error = QStringLiteral("missing or non-numeric field '%1'").arg(key);
            }
            return false;
        }
        const qint64 v = value.toInteger();
        if (v < lo || v > hi) {
            if (error) {
                *error = QStringLiteral("field '%1' value %2 is out of range").arg(key).arg(v);
            }
            return false;
        }
        out = v;
        return true;
    }

    // Required integer field in [lo, hi].
    bool readRequired(const QJsonObject& json, const QString& key, qint64 lo, qint64 hi, qint64& out, QString* error) {
        return checkInt(json.value(key), lo, hi, key, out, error);
    }

    // Optional integer field: absent => `fallback`; present => validated against [lo, hi]
    // (a present-but-wrong-type or out-of-range value is an error, not a default).
    bool readOptional(const QJsonObject& json, const QString& key, qint64 lo, qint64 hi, qint64 fallback, qint64& out, QString* error) {
        if (!json.contains(key)) {
            out = fallback;
            return true;
        }
        return checkInt(json.value(key), lo, hi, key, out, error);
    }

    bool parseObject(const QJsonValue& value, PatternObject& out, QString* error) {
        if (!value.isObject()) {
            if (error) {
                *error = QStringLiteral("object entry is not a JSON object");
            }
            return false;
        }
        const QJsonObject json = value.toObject();

        qint64 dxHex = 0;
        qint64 dyHex = 0;
        qint64 proPid = 0;
        qint64 frmPid = 0;
        qint64 direction = 0;
        qint64 flags = 0;
        if (!readRequired(json, QStringLiteral("dxHex"), INT32_LO, INT32_HI, dxHex, error)
            || !readRequired(json, QStringLiteral("dyHex"), INT32_LO, INT32_HI, dyHex, error)
            || !readRequired(json, QStringLiteral("proPid"), 0, U32_HI, proPid, error)
            || !readRequired(json, QStringLiteral("frmPid"), 0, U32_HI, frmPid, error)
            || !readOptional(json, QStringLiteral("direction"), 0, U32_HI, 0, direction, error)
            || !readOptional(json, QStringLiteral("flags"), 0, U32_HI, 0, flags, error)) {
            return false;
        }

        out.dxHex = static_cast<int>(dxHex);
        out.dyHex = static_cast<int>(dyHex);
        out.proPid = static_cast<uint32_t>(proPid);
        out.frmPid = static_cast<uint32_t>(frmPid);
        out.direction = static_cast<uint32_t>(direction);
        out.flags = static_cast<uint32_t>(flags);
        return true;
    }

    bool parseTile(const QJsonValue& value, PatternTile& out, QString* error) {
        if (!value.isObject()) {
            if (error) {
                *error = QStringLiteral("tile entry is not a JSON object");
            }
            return false;
        }
        const QJsonObject json = value.toObject();

        qint64 dxTile = 0;
        qint64 dyTile = 0;
        qint64 tileId = 0;
        if (!readRequired(json, QStringLiteral("dxTile"), INT32_LO, INT32_HI, dxTile, error)
            || !readRequired(json, QStringLiteral("dyTile"), INT32_LO, INT32_HI, dyTile, error)
            || !readRequired(json, QStringLiteral("tileId"), 0, U16_HI, tileId, error)) {
            return false;
        }

        out.dxTile = static_cast<int>(dxTile);
        out.dyTile = static_cast<int>(dyTile);
        out.tileId = static_cast<uint16_t>(tileId);
        return true;
    }

    // Parses a "floor"/"roof"/"objects" array (absent => empty). Returns false on a
    // malformed entry.
    template <typename T, typename ParseFn>
    bool parseArray(const QJsonObject& root, const QString& key, std::vector<T>& out, ParseFn parse, QString* error) {
        if (!root.contains(key)) {
            return true; // optional section
        }
        const QJsonValue value = root.value(key);
        if (!value.isArray()) {
            if (error) {
                *error = QStringLiteral("field '%1' is not an array").arg(key);
            }
            return false;
        }
        for (const QJsonValue& entry : value.toArray()) {
            T parsed;
            if (!parse(entry, parsed, error)) {
                if (error) {
                    *error = QStringLiteral("in '%1': %2").arg(key, *error);
                }
                return false;
            }
            out.push_back(parsed);
        }
        return true;
    }

} // namespace

QByteArray PatternSerializer::serialize(const Pattern& pattern) {
    QJsonObject root;
    root["name"] = QString::fromStdString(pattern.name);
    root["version"] = pattern.version;
    root["anchorHex"] = pattern.anchorHex;

    QJsonObject size;
    size["hexW"] = pattern.sizeHexW;
    size["hexH"] = pattern.sizeHexH;
    root["size"] = size;

    root["rotatable"] = pattern.rotatable;

    QJsonArray objects;
    for (const PatternObject& obj : pattern.objects) {
        objects.append(objectToJson(obj));
    }
    root["objects"] = objects;

    QJsonArray floor;
    for (const PatternTile& tile : pattern.floor) {
        floor.append(tileToJson(tile));
    }
    root["floor"] = floor;

    QJsonArray roof;
    for (const PatternTile& tile : pattern.roof) {
        roof.append(tileToJson(tile));
    }
    root["roof"] = roof;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<Pattern> PatternSerializer::deserialize(const QByteArray& data, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("invalid JSON: %1").arg(parseError.errorString());
        }
        return std::nullopt;
    }
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("top-level JSON value is not an object");
        }
        return std::nullopt;
    }
    const QJsonObject root = doc.object();

    qint64 version = 0;
    if (!readRequired(root, QStringLiteral("version"), INT32_LO, INT32_HI, version, error)) {
        return std::nullopt;
    }
    if (version != Pattern::CURRENT_VERSION) {
        if (error) {
            *error = QStringLiteral("unsupported pattern version %1 (expected %2)")
                         .arg(version)
                         .arg(Pattern::CURRENT_VERSION);
        }
        return std::nullopt;
    }

    const QJsonValue nameValue = root.value(QStringLiteral("name"));
    if (!nameValue.isString()) {
        if (error) {
            *error = QStringLiteral("missing or non-string field 'name'");
        }
        return std::nullopt;
    }

    qint64 anchorHex = 0;
    if (!readRequired(root, QStringLiteral("anchorHex"), INT32_LO, INT32_HI, anchorHex, error)) {
        return std::nullopt;
    }

    const QJsonValue sizeValue = root.value(QStringLiteral("size"));
    if (!sizeValue.isObject()) {
        if (error) {
            *error = QStringLiteral("missing or non-object field 'size'");
        }
        return std::nullopt;
    }
    const QJsonObject size = sizeValue.toObject();
    qint64 hexW = 0;
    qint64 hexH = 0;
    if (!readRequired(size, QStringLiteral("hexW"), INT32_LO, INT32_HI, hexW, error)
        || !readRequired(size, QStringLiteral("hexH"), INT32_LO, INT32_HI, hexH, error)) {
        return std::nullopt;
    }

    Pattern pattern;
    pattern.version = static_cast<int>(version);
    pattern.name = nameValue.toString().toStdString();
    pattern.anchorHex = static_cast<int>(anchorHex);
    pattern.sizeHexW = static_cast<int>(hexW);
    pattern.sizeHexH = static_cast<int>(hexH);
    pattern.rotatable = root.value(QStringLiteral("rotatable")).toBool(false);

    if (!parseArray(root, QStringLiteral("objects"), pattern.objects, parseObject, error)
        || !parseArray(root, QStringLiteral("floor"), pattern.floor, parseTile, error)
        || !parseArray(root, QStringLiteral("roof"), pattern.roof, parseTile, error)) {
        return std::nullopt;
    }

    return pattern;
}

} // namespace geck::pattern
