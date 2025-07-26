#include "Settings.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonParseError>
#include <QDebug>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <QSettings>
#include <windows.h>
#endif

namespace geck {

Settings::Settings()
    : _version(SETTINGS_VERSION) {
}

Settings& Settings::getInstance() {
    static Settings instance;
    return instance;
}

QString Settings::getSettingsFilePath() const {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir configDir(configPath);
    
    // Create organization directory if it doesn't exist
    if (!configDir.exists(ORGANIZATION_NAME)) {
        configDir.mkpath(ORGANIZATION_NAME);
    }
    
    configDir.cd(ORGANIZATION_NAME);
    return configDir.absoluteFilePath(SETTINGS_FILENAME);
}

bool Settings::exists() const {
    return QFile::exists(getSettingsFilePath());
}

void Settings::load() {
    QString filePath = getSettingsFilePath();
    
    if (!QFile::exists(filePath)) {
        spdlog::info("Settings file not found, using defaults: {}", filePath.toStdString());
        return;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::error("Failed to open settings file for reading: {}", filePath.toStdString());
        return;
    }
    
    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        spdlog::error("Failed to parse settings JSON: {}", parseError.errorString().toStdString());
        return;
    }
    
    fromJson(doc.object());
    spdlog::info("Settings loaded from: {}", filePath.toStdString());
}

void Settings::save() {
    QString filePath = getSettingsFilePath();
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        spdlog::error("Failed to open settings file for writing: {}", filePath.toStdString());
        return;
    }
    
    QJsonDocument doc(toJson());
    file.write(doc.toJson());
    
    spdlog::info("Settings saved to: {}", filePath.toStdString());
}

QJsonObject Settings::toJson() const {
    QJsonObject json;
    
    json["version"] = _version;
    json["dataPaths"] = pathVectorToJsonArray(_dataPaths);
    
    // UI state
    QJsonObject ui;
    if (!_windowGeometry.isEmpty()) {
        ui["windowGeometry"] = QString::fromLatin1(_windowGeometry.toBase64());
    }
    if (!_dockState.isEmpty()) {
        ui["dockState"] = QString::fromLatin1(_dockState.toBase64());
    }
    
    // Floating dock geometries
    if (!_floatingDockGeometries.isEmpty()) {
        QJsonObject floatingDocks;
        for (auto it = _floatingDockGeometries.begin(); it != _floatingDockGeometries.end(); ++it) {
            floatingDocks[it.key()] = QString::fromLatin1(it.value().toBase64());
        }
        ui["floatingDockGeometries"] = floatingDocks;
    }
    
    json["ui"] = ui;
    
    return json;
}

void Settings::fromJson(const QJsonObject& json) {
    _version = json["version"].toString(SETTINGS_VERSION);

    // Data paths
    if (json.contains("dataPaths")) {
        _dataPaths = jsonArrayToPathVector(json["dataPaths"].toArray());
    }
    
    // UI state
    if (json.contains("ui")) {
        QJsonObject ui = json["ui"].toObject();
        
        if (ui.contains("windowGeometry")) {
            _windowGeometry = QByteArray::fromBase64(ui["windowGeometry"].toString().toLatin1());
        }
        
        if (ui.contains("dockState")) {
            _dockState = QByteArray::fromBase64(ui["dockState"].toString().toLatin1());
        }
        
        if (ui.contains("floatingDockGeometries")) {
            QJsonObject floatingDocks = ui["floatingDockGeometries"].toObject();
            for (auto it = floatingDocks.begin(); it != floatingDocks.end(); ++it) {
                _floatingDockGeometries[it.key()] = QByteArray::fromBase64(it.value().toString().toLatin1());
            }
        }
    }
}

QJsonArray Settings::pathVectorToJsonArray(const std::vector<std::filesystem::path>& paths) const {
    QJsonArray array;
    for (const auto& path : paths) {
        array.append(QString::fromStdString(path.string()));
    }
    return array;
}

std::vector<std::filesystem::path> Settings::jsonArrayToPathVector(const QJsonArray& array) const {
    std::vector<std::filesystem::path> paths;
    for (const auto& value : array) {
        if (value.isString()) {
            paths.emplace_back(value.toString().toStdString());
        }
    }
    return paths;
}

// Data paths management
void Settings::addDataPath(const std::filesystem::path& path) {
    // Check if path already exists
    for (const auto& existingPath : _dataPaths) {
        if (std::filesystem::equivalent(existingPath, path)) {
            spdlog::debug("Data path already exists: {}", path.string());
            return;
        }
    }
    
    _dataPaths.push_back(path);
    spdlog::info("Added data path: {}", path.string());
}

void Settings::removeDataPath(const std::filesystem::path& path) {
    auto it = std::remove_if(_dataPaths.begin(), _dataPaths.end(),
        [&path](const std::filesystem::path& existing) {
            return std::filesystem::equivalent(existing, path);
        });
    
    if (it != _dataPaths.end()) {
        _dataPaths.erase(it, _dataPaths.end());
        spdlog::info("Removed data path: {}", path.string());
    }
}

std::vector<std::filesystem::path> Settings::getDataPaths() const {
    return _dataPaths;
}

void Settings::setDataPaths(const std::vector<std::filesystem::path>& paths) {
    _dataPaths = paths;
}

// UI state management
QByteArray Settings::getWindowGeometry() const {
    return _windowGeometry;
}

void Settings::setWindowGeometry(const QByteArray& geometry) {
    _windowGeometry = geometry;
}

QByteArray Settings::getDockState() const {
    return _dockState;
}

void Settings::setDockState(const QByteArray& state) {
    _dockState = state;
}

QByteArray Settings::getFloatingDockGeometry(const QString& dockName) const {
    return _floatingDockGeometries.value(dockName);
}

void Settings::setFloatingDockGeometry(const QString& dockName, const QByteArray& geometry) {
    _floatingDockGeometries[dockName] = geometry;
}

// Validation
bool Settings::validateDataPath(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        return false;
    }
    
    if (std::filesystem::is_directory(path)) {
        // Check for common Fallout 2 files
        return std::filesystem::exists(path / "master.dat") || 
               std::filesystem::exists(path / "critter.dat") ||
               std::filesystem::exists(path / "maps");
    } else if (path.extension() == ".dat") {
        return std::filesystem::is_regular_file(path);
    }
    
    return false;
}

// Auto-detection methods
std::vector<std::filesystem::path> Settings::detectFallout2Installations() {
    std::vector<std::filesystem::path> installations;
    
#ifdef _WIN32
    // Windows registry detection
    
    // Check GOG installation
    QSettings gogRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\GOG.com\\Games\\1207659119", QSettings::NativeFormat);
    QString gogPath = gogRegistry.value("path").toString();
    if (!gogPath.isEmpty()) {
        std::filesystem::path dataPath = std::filesystem::path(gogPath.toStdString()) / "data";
        if (std::filesystem::exists(dataPath)) {
            installations.push_back(dataPath);
            spdlog::info("Found GOG Fallout 2 installation: {}", dataPath.string());
        }
    }
    
    // Check Steam installation
    QSettings steamRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam", QSettings::NativeFormat);
    QString steamPath = steamRegistry.value("InstallPath").toString();
    if (!steamPath.isEmpty()) {
        std::filesystem::path steamFo2Path = std::filesystem::path(steamPath.toStdString()) / 
                                           "steamapps" / "common" / "Fallout 2" / "data";
        if (std::filesystem::exists(steamFo2Path)) {
            installations.push_back(steamFo2Path);
            spdlog::info("Found Steam Fallout 2 installation: {}", steamFo2Path.string());
        }
    }
    
#elif defined(__APPLE__)
    // macOS common paths
    std::vector<std::filesystem::path> macPaths = {
        "/Applications/Fallout 2.app/Contents/Resources/data",
        "/Applications/Fallout 2.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2/data",
        std::filesystem::path(QDir::homePath().toStdString()) / "Applications/Fallout 2.app/Contents/Resources/data"
    };
    
    for (const auto& path : macPaths) {
        if (std::filesystem::exists(path)) {
            installations.push_back(path);
            spdlog::info("Found macOS Fallout 2 installation: {}", path.string());
        }
    }
    
#else
    // Linux common paths
    std::vector<std::filesystem::path> linuxPaths = {
        std::filesystem::path(QDir::homePath().toStdString()) / ".local/share/Steam/steamapps/common/Fallout 2/data",
        "/usr/local/games/fallout2/data",
        "/opt/fallout2/data"
    };
    
    for (const auto& path : linuxPaths) {
        if (std::filesystem::exists(path)) {
            installations.push_back(path);
            spdlog::info("Found Linux Fallout 2 installation: {}", path.string());
        }
    }
#endif
    
    // Steam library detection (cross-platform)
    auto steamLibraries = detectSteamLibraries();
    for (const auto& library : steamLibraries) {
        std::filesystem::path fo2Path = library / "steamapps" / "common" / "Fallout 2" / "data";
        if (std::filesystem::exists(fo2Path)) {
            installations.push_back(fo2Path);
            spdlog::info("Found Fallout 2 in Steam library: {}", fo2Path.string());
        }
    }
    
    return installations;
}

std::vector<std::filesystem::path> Settings::detectSteamLibraries() {
    std::vector<std::filesystem::path> libraries;
    
    // TODO: Implement Steam library.vdf parsing
    // This would involve parsing the Steam configuration to find additional library folders
    
    return libraries;
}

} // namespace geck