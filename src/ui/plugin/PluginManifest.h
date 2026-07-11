#pragma once

#include <QString>

#include <optional>

namespace geck::plugin {

/// A plugin's `plugin.json`, parsed and validated in C++ before any plugin code runs — identity
/// (and, in a later phase, permissions) must be known before the VM starts, so the manifest is
/// data, never executed Lua. No silent fallbacks: a malformed document, a missing or blank
/// required field, an unsafe `id`, or an `entry` path that could escape the plugin directory is a
/// hard error with a reason, mirroring PatternSerializer's discipline.
struct PluginManifest {
    QString id;          ///< Required. Stable identity + dedup key; a single safe path component.
    QString name;        ///< Required. Human-readable display name.
    QString version;     ///< Required. Free-form version string (shown, not yet compared).
    QString entry;       ///< Required. Entry `.luau`, a relative path confined to the plugin dir.
    QString description; ///< Optional. One-line summary for the manager list.
    QString author;      ///< Optional.

    /// Parse and validate `plugin.json` text. Returns the manifest, or nullopt with `error` set
    /// on any failure: malformed JSON, a non-object root, a missing/blank required field, an `id`
    /// that is not a safe path component, or an `entry` that is absolute or contains a `..`
    /// segment (which could read outside the plugin directory).
    static std::optional<PluginManifest> parse(const QByteArray& json, QString& error);

    /// True if `id` is a single safe path component: non-empty, not "." or "..", and made only of
    /// letters, digits, '.', '_' or '-' (so it is usable verbatim as a directory / storage key).
    static bool isValidId(const QString& id);

    /// True if `entry` is a plugin-dir-relative path: non-empty, not absolute, and with no ".."
    /// segment — the confinement check the loader relies on before reading the entry file.
    static bool isSafeRelativePath(const QString& path);
};

} // namespace geck::plugin
