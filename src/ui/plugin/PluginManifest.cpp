#include "ui/plugin/PluginManifest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

namespace geck::plugin {

namespace {

    // A required string field: must be present, be a JSON string, and be non-blank once trimmed.
    // Returns false with `error` set otherwise — never substitutes a default.
    bool requireString(const QJsonObject& obj, const QString& key, QString& out, QString& error) {
        if (!obj.contains(key)) {
            error = QStringLiteral("missing required field \"%1\"").arg(key);
            return false;
        }
        const QJsonValue value = obj.value(key);
        if (!value.isString()) {
            error = QStringLiteral("field \"%1\" must be a string").arg(key);
            return false;
        }
        out = value.toString().trimmed();
        if (out.isEmpty()) {
            error = QStringLiteral("field \"%1\" must not be blank").arg(key);
            return false;
        }
        return true;
    }

    // An optional string field: absent -> empty; present but not a string -> error (a wrong type is a
    // mistake worth surfacing, not something to silently ignore).
    bool optionalString(const QJsonObject& obj, const QString& key, QString& out, QString& error) {
        if (!obj.contains(key) || obj.value(key).isNull()) {
            out.clear();
            return true;
        }
        const QJsonValue value = obj.value(key);
        if (!value.isString()) {
            error = QStringLiteral("field \"%1\" must be a string").arg(key);
            return false;
        }
        out = value.toString().trimmed();
        return true;
    }

} // namespace

bool PluginManifest::isValidId(const QString& id) {
    if (id.isEmpty() || id == QLatin1String(".") || id == QLatin1String("..")) {
        return false;
    }
    for (const QChar c : id) {
        const bool ok = c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('_')
            || c == QLatin1Char('-');
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool PluginManifest::isSafeRelativePath(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    // Absolute paths (POSIX "/…" or a Windows "C:\…" drive) reach outside the plugin dir.
    if (path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))) {
        return false;
    }
    if (path.size() >= 2 && path.at(1) == QLatin1Char(':')) {
        return false;
    }
    // Reject any ".." segment (using either separator), which would climb out of the dir.
    const QStringList segments = path.split(QRegularExpression(QStringLiteral("[/\\\\]")));
    for (const QString& segment : segments) {
        if (segment == QLatin1String("..")) {
            return false;
        }
    }
    return true;
}

std::optional<PluginManifest> PluginManifest::parse(const QByteArray& json, QString& error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QStringLiteral("invalid JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }
    if (!doc.isObject()) {
        error = QStringLiteral("the manifest root must be a JSON object");
        return std::nullopt;
    }
    const QJsonObject obj = doc.object();

    PluginManifest manifest;
    if (!requireString(obj, QStringLiteral("id"), manifest.id, error)
        || !requireString(obj, QStringLiteral("name"), manifest.name, error)
        || !requireString(obj, QStringLiteral("version"), manifest.version, error)
        || !requireString(obj, QStringLiteral("entry"), manifest.entry, error)
        || !optionalString(obj, QStringLiteral("description"), manifest.description, error)
        || !optionalString(obj, QStringLiteral("author"), manifest.author, error)) {
        return std::nullopt;
    }

    if (!isValidId(manifest.id)) {
        error = QStringLiteral(
            "\"id\" must be a single path component of letters, digits, '.', '_' or '-' (got \"%1\")")
                    .arg(manifest.id);
        return std::nullopt;
    }
    if (!isSafeRelativePath(manifest.entry)) {
        error = QStringLiteral(
            "\"entry\" must be a relative path inside the plugin directory (got \"%1\")")
                    .arg(manifest.entry);
        return std::nullopt;
    }

    return manifest;
}

} // namespace geck::plugin
