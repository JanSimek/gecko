#pragma once

#include <QString>
#include <QStringList>
#include <QColor>
#include <QMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace geck {

/**
 * @brief Application settings management with JSON persistence
 *
 * Handles application configuration including data paths, UI state,
 * and user preferences. Settings are stored in JSON format for
 * easy manual editing.
 */
class Settings {
public:
    // Constructed and owned at the Application root, then injected (mirroring
    // GameResources); there is no global singleton.
    Settings();

    // Disable copy/assignment
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    // Settings management
    void load();
    void save();
    bool exists() const;

    // Data paths management
    void addDataPath(const std::filesystem::path& path);
    void removeDataPath(const std::filesystem::path& path);
    std::vector<std::filesystem::path> getDataPaths() const;
    void setDataPaths(const std::vector<std::filesystem::path>& paths);

    // UI state
    QByteArray getWindowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);

    QByteArray getDockState() const;
    void setDockState(const QByteArray& state);

    // Panel visibility preferences
    std::optional<bool> getPanelVisibilityPreference(const QString& panelName) const;
    void setPanelVisibilityPreference(const QString& panelName, bool visible);

    // Window state (normal/maximized)
    bool getWindowMaximized() const;
    void setWindowMaximized(bool maximized);

    // Whether touching same-category selected objects merge into one outline (View menu toggle).
    bool getMergeSelectionOutlines() const;
    void setMergeSelectionOutlines(bool merge);

    // Whether the cursor near a viewport edge auto-scrolls the map (View menu toggle).
    bool getEdgeScrollEnabled() const;
    void setEdgeScrollEnabled(bool enabled);

    // Floating dock geometries
    QByteArray getFloatingDockGeometry(const QString& dockName) const;
    void setFloatingDockGeometry(const QString& dockName, const QByteArray& geometry);

    // Settings validation
    bool validateDataPath(const std::filesystem::path& path) const;
    static std::filesystem::path normalizeDataPath(const std::filesystem::path& path);

    // Text editor configuration
    enum class TextEditorMode {
        SYSTEM_DEFAULT,
        CUSTOM
    };

    TextEditorMode getTextEditorMode() const;
    void setTextEditorMode(TextEditorMode mode);
    QString getCustomEditorPath() const;
    void setCustomEditorPath(const QString& path);

    // Selection highlight colours (preferences). Keys: "object", "wall", "critter", "tile".
    // Returns @p fallback when the colour has not been configured.
    QColor getSelectionColor(const QString& key, const QColor& fallback) const;
    void setSelectionColor(const QString& key, const QColor& color);

    // Game location configuration
    std::filesystem::path getGameLocation() const;
    bool isGameLocationValid() const;
    void autoDetectGameLocation();

    std::filesystem::path getExecutableGameLocation() const;
    void setExecutableGameLocation(const std::filesystem::path& location);

    std::filesystem::path getGameDataDirectory() const;
    void setGameDataDirectory(const std::filesystem::path& location);

    // Auto-detection helpers
    static std::vector<std::filesystem::path> detectFallout2Installations();

    struct DetectedInstallation {
        std::filesystem::path path;
        std::string description;
    };

    static std::vector<DetectedInstallation> detectFallout2InstallationsDetailed();

private:
    // JSON serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    // Helper methods
    QString getSettingsFilePath() const;
    QJsonArray pathVectorToJsonArray(const std::vector<std::filesystem::path>& paths) const;
    std::vector<std::filesystem::path> jsonArrayToPathVector(const QJsonArray& array) const;

    // Settings data
    std::vector<std::filesystem::path> _dataPaths;
    QByteArray _windowGeometry;
    QByteArray _dockState;
    bool _windowMaximized = true;        // Default to maximized
    bool _mergeSelectionOutlines = true; // Default: merge touching same-category outlines
    bool _edgeScrollEnabled = true;      // Default: auto-scroll when the cursor rests near an edge
    QMap<QString, QByteArray> _floatingDockGeometries;
    QMap<QString, bool> _panelVisibilityPreferences;
    QString _version;

    // Text editor configuration
    TextEditorMode _textEditorMode = TextEditorMode::SYSTEM_DEFAULT;
    QString _customEditorPath;

    // Selection highlight colour overrides (empty = use the renderer defaults).
    QMap<QString, QColor> _selectionColors;

    // Game location configuration
    std::filesystem::path _executableGameLocation;
    std::filesystem::path _gameDataDirectory;

    // Constants
    // 1.1: data paths store each DAT as an explicit entry (no silent nested-mounting). fromJson
    // migrates older "1.0" settings by expanding folders into folder + master.dat/critter.dat.
    static constexpr const char* SETTINGS_VERSION = "1.1";
    static constexpr const char* SETTINGS_FILENAME = "settings.json";
    static constexpr const char* ORGANIZATION_NAME = "gecko";
    static constexpr const char* APPLICATION_NAME = "editor";
};

} // namespace geck
