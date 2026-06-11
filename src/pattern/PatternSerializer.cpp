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

    template <typename T, typename ToJson>
    QJsonArray arrayToJson(const std::vector<T>& items, ToJson toJson) {
        QJsonArray out;
        for (const T& item : items) {
            out.append(toJson(item));
        }
        return out;
    }

    QJsonObject variantToJson(const PatternVariant& variant) {
        QJsonObject json;
        json["label"] = QString::fromStdString(variant.label);
        json["anchorHex"] = variant.anchorHex;
        json["objects"] = arrayToJson(variant.objects, objectToJson);
        json["floor"] = arrayToJson(variant.floor, tileToJson);
        json["roof"] = arrayToJson(variant.roof, tileToJson);
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

    // Parses an "objects"/"floor"/"roof" array (absent => empty). Returns false on a
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

    bool parseVariant(const QJsonValue& value, PatternVariant& out, QString* error) {
        if (!value.isObject()) {
            if (error) {
                *error = QStringLiteral("variant entry is not a JSON object");
            }
            return false;
        }
        const QJsonObject json = value.toObject();

        qint64 anchorHex = 0;
        if (!readRequired(json, QStringLiteral("anchorHex"), INT32_LO, INT32_HI, anchorHex, error)) {
            return false;
        }
        out.anchorHex = static_cast<int>(anchorHex);
        out.label = json.value(QStringLiteral("label")).toString().toStdString();

        return parseArray(json, QStringLiteral("objects"), out.objects, parseObject, error)
            && parseArray(json, QStringLiteral("floor"), out.floor, parseTile, error)
            && parseArray(json, QStringLiteral("roof"), out.roof, parseTile, error);
    }

} // namespace

QByteArray PatternSerializer::serialize(const Pattern& pattern) {
    QJsonObject root;
    root["name"] = QString::fromStdString(pattern.name);
    root["version"] = pattern.version;
    root["variants"] = arrayToJson(pattern.variants, variantToJson);
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

    const QJsonValue variantsValue = root.value(QStringLiteral("variants"));
    if (!variantsValue.isArray()) {
        if (error) {
            *error = QStringLiteral("missing or non-array field 'variants'");
        }
        return std::nullopt;
    }
    const QJsonArray variantsArray = variantsValue.toArray();
    if (variantsArray.isEmpty()) {
        if (error) {
            *error = QStringLiteral("a pattern must have at least one variant");
        }
        return std::nullopt;
    }

    Pattern pattern;
    pattern.version = static_cast<int>(version);
    pattern.name = nameValue.toString().toStdString();
    for (const QJsonValue& entry : variantsArray) {
        PatternVariant variant;
        if (!parseVariant(entry, variant, error)) {
            if (error) {
                *error = QStringLiteral("in 'variants': %1").arg(*error);
            }
            return std::nullopt;
        }
        pattern.variants.push_back(std::move(variant));
    }

    return pattern;
}

} // namespace geck::pattern
