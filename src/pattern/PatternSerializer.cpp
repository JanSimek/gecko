#include "pattern/PatternSerializer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace geck::pattern {

namespace {

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

    // Reads a required integer-valued field. Returns false (and sets `error`) when the
    // field is absent or not a JSON number.
    bool readRequiredInt(const QJsonObject& json, const QString& key, qint64& out, QString* error) {
        const QJsonValue value = json.value(key);
        if (!value.isDouble()) {
            if (error) {
                *error = QStringLiteral("missing or non-numeric field '%1'").arg(key);
            }
            return false;
        }
        out = value.toInteger();
        return true;
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
        if (!readRequiredInt(json, QStringLiteral("dxHex"), dxHex, error)
            || !readRequiredInt(json, QStringLiteral("dyHex"), dyHex, error)
            || !readRequiredInt(json, QStringLiteral("proPid"), proPid, error)
            || !readRequiredInt(json, QStringLiteral("frmPid"), frmPid, error)) {
            return false;
        }

        out.dxHex = static_cast<int>(dxHex);
        out.dyHex = static_cast<int>(dyHex);
        out.proPid = static_cast<uint32_t>(proPid);
        out.frmPid = static_cast<uint32_t>(frmPid);
        // direction and flags are optional; default to 0.
        out.direction = static_cast<uint32_t>(json.value(QStringLiteral("direction")).toInteger(0));
        out.flags = static_cast<uint32_t>(json.value(QStringLiteral("flags")).toInteger(0));
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
        if (!readRequiredInt(json, QStringLiteral("dxTile"), dxTile, error)
            || !readRequiredInt(json, QStringLiteral("dyTile"), dyTile, error)
            || !readRequiredInt(json, QStringLiteral("tileId"), tileId, error)) {
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
    if (!readRequiredInt(root, QStringLiteral("version"), version, error)) {
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

    qint64 anchorHex = 0;
    if (!readRequiredInt(root, QStringLiteral("anchorHex"), anchorHex, error)) {
        return std::nullopt;
    }

    Pattern pattern;
    pattern.version = static_cast<int>(version);
    pattern.name = root.value(QStringLiteral("name")).toString().toStdString();
    pattern.anchorHex = static_cast<int>(anchorHex);
    pattern.rotatable = root.value(QStringLiteral("rotatable")).toBool(false);

    const QJsonObject size = root.value(QStringLiteral("size")).toObject();
    pattern.sizeHexW = static_cast<int>(size.value(QStringLiteral("hexW")).toInteger(0));
    pattern.sizeHexH = static_cast<int>(size.value(QStringLiteral("hexH")).toInteger(0));

    if (!parseArray(root, QStringLiteral("objects"), pattern.objects, parseObject, error)
        || !parseArray(root, QStringLiteral("floor"), pattern.floor, parseTile, error)
        || !parseArray(root, QStringLiteral("roof"), pattern.roof, parseTile, error)) {
        return std::nullopt;
    }

    return pattern;
}

} // namespace geck::pattern
