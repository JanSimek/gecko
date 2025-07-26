#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <filesystem>
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
    // Singleton access
    static Settings& getInstance();
    
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
    
    // Floating dock geometries
    QByteArray getFloatingDockGeometry(const QString& dockName) const;
    void setFloatingDockGeometry(const QString& dockName, const QByteArray& geometry);

    // Settings validation
    bool validateDataPath(const std::filesystem::path& path) const;
    
    // Auto-detection helpers
    static std::vector<std::filesystem::path> detectFallout2Installations();
    static std::vector<std::filesystem::path> detectSteamLibraries();

private:
    Settings();
    ~Settings() = default;
    
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
    QMap<QString, QByteArray> _floatingDockGeometries;
    QString _version;
    
    // Constants
    static constexpr const char* SETTINGS_VERSION = "1.0";
    static constexpr const char* SETTINGS_FILENAME = "settings.json";
    static constexpr const char* ORGANIZATION_NAME = "gecko";
    static constexpr const char* APPLICATION_NAME = "editor";
};

} // namespace geck