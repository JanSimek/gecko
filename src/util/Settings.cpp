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
    ui["windowMaximized"] = _windowMaximized;
    
    // Floating dock geometries
    if (!_floatingDockGeometries.isEmpty()) {
        QJsonObject floatingDocks;
        for (auto it = _floatingDockGeometries.begin(); it != _floatingDockGeometries.end(); ++it) {
            floatingDocks[it.key()] = QString::fromLatin1(it.value().toBase64());
        }
        ui["floatingDockGeometries"] = floatingDocks;
    }
    
    json["ui"] = ui;
    
    // Text editor configuration
    QJsonObject textEditor;
    textEditor["mode"] = (_textEditorMode == TextEditorMode::SYSTEM_DEFAULT) ? "system" : "custom";
    if (!_customEditorPath.isEmpty()) {
        textEditor["customPath"] = _customEditorPath;
    }
    json["textEditor"] = textEditor;
    
    // Game location configuration
    QJsonObject gameLocation;
    gameLocation["installationType"] = (_gameInstallationType == GameInstallationType::STEAM) ? "steam" : "executable";
    
    if (!_steamAppId.empty()) {
        gameLocation["steamAppId"] = QString::fromStdString(_steamAppId);
    }
    if (!_executableGameLocation.empty()) {
        gameLocation["executablePath"] = QString::fromStdString(_executableGameLocation.string());
    }
    if (!_gameDataDirectory.empty()) {
        gameLocation["dataDirectory"] = QString::fromStdString(_gameDataDirectory.string());
    }
    
    // Backward compatibility
    if (!_gameLocation.empty()) {
        gameLocation["legacyPath"] = QString::fromStdString(_gameLocation.string());
    }
    
    json["gameLocation"] = gameLocation;
    
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
        
        if (ui.contains("windowMaximized")) {
            _windowMaximized = ui["windowMaximized"].toBool(true); // Default to true
        }
        
        if (ui.contains("floatingDockGeometries")) {
            QJsonObject floatingDocks = ui["floatingDockGeometries"].toObject();
            for (auto it = floatingDocks.begin(); it != floatingDocks.end(); ++it) {
                _floatingDockGeometries[it.key()] = QByteArray::fromBase64(it.value().toString().toLatin1());
            }
        }
    }
    
    // Text editor configuration
    if (json.contains("textEditor")) {
        QJsonObject textEditor = json["textEditor"].toObject();
        
        QString mode = textEditor["mode"].toString("system");
        _textEditorMode = (mode == "custom") ? TextEditorMode::CUSTOM : TextEditorMode::SYSTEM_DEFAULT;
        
        if (textEditor.contains("customPath")) {
            _customEditorPath = textEditor["customPath"].toString();
        }
    }
    
    // Game location configuration
    if (json.contains("gameLocation")) {
        QJsonValue gameLocationValue = json["gameLocation"];
        
        if (gameLocationValue.isString()) {
            // Backward compatibility - old format
            QString gameLocationStr = gameLocationValue.toString();
            if (!gameLocationStr.isEmpty()) {
                _gameLocation = std::filesystem::path(gameLocationStr.toStdString());
                _executableGameLocation = _gameLocation; // Default to executable type
            }
        } else if (gameLocationValue.isObject()) {
            // New format
            QJsonObject gameLocation = gameLocationValue.toObject();
            
            QString installationType = gameLocation["installationType"].toString("executable");
            _gameInstallationType = (installationType == "steam") ? GameInstallationType::STEAM : GameInstallationType::EXECUTABLE;
            
            if (gameLocation.contains("steamAppId")) {
                QString steamAppId = gameLocation["steamAppId"].toString();
                if (!steamAppId.isEmpty()) {
                    _steamAppId = steamAppId.toStdString();
                }
            }
            
            // Legacy support for steamPath (convert to default app ID)
            if (gameLocation.contains("steamPath")) {
                QString steamPath = gameLocation["steamPath"].toString();
                if (!steamPath.isEmpty() && _steamAppId.empty()) {
                    _steamAppId = "38410"; // Default Fallout 2 Steam App ID
                }
            }
            
            if (gameLocation.contains("executablePath")) {
                QString executablePath = gameLocation["executablePath"].toString();
                if (!executablePath.isEmpty()) {
                    _executableGameLocation = std::filesystem::path(executablePath.toStdString());
                }
            }
            
            if (gameLocation.contains("dataDirectory")) {
                QString dataDirectory = gameLocation["dataDirectory"].toString();
                if (!dataDirectory.isEmpty()) {
                    _gameDataDirectory = std::filesystem::path(dataDirectory.toStdString());
                }
            }
            
            // Backward compatibility
            if (gameLocation.contains("legacyPath")) {
                QString legacyPath = gameLocation["legacyPath"].toString();
                if (!legacyPath.isEmpty()) {
                    _gameLocation = std::filesystem::path(legacyPath.toStdString());
                    // If no specific paths are set, use legacy path for current type
                    if (_steamAppId.empty() && _executableGameLocation.empty()) {
                        if (_gameInstallationType == GameInstallationType::STEAM) {
                            _steamAppId = "38410"; // Default Steam App ID
                        } else {
                            _executableGameLocation = _gameLocation;
                        }
                    }
                }
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

bool Settings::getWindowMaximized() const {
    return _windowMaximized;
}

void Settings::setWindowMaximized(bool maximized) {
    _windowMaximized = maximized;
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

// Text editor configuration
Settings::TextEditorMode Settings::getTextEditorMode() const {
    return _textEditorMode;
}

void Settings::setTextEditorMode(TextEditorMode mode) {
    _textEditorMode = mode;
}

QString Settings::getCustomEditorPath() const {
    return _customEditorPath;
}

void Settings::setCustomEditorPath(const QString& path) {
    _customEditorPath = path;
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

std::vector<Settings::DetectedInstallation> Settings::detectFallout2InstallationsDetailed() {
    std::vector<DetectedInstallation> installations;
    
#ifdef _WIN32
    // Windows registry detection
    
    // Check GOG installation
    QSettings gogRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\GOG.com\\Games\\1207659119", QSettings::NativeFormat);
    QString gogPath = gogRegistry.value("path").toString();
    if (!gogPath.isEmpty()) {
        std::filesystem::path gogGamePath = std::filesystem::path(gogPath.toStdString());
        std::filesystem::path dataPath = gogGamePath / "data";
        if (std::filesystem::exists(dataPath)) {
            installations.push_back({gogGamePath, GameInstallationType::EXECUTABLE, "GOG Installation"});
            spdlog::info("Found GOG Fallout 2 installation: {}", gogGamePath.string());
        }
    }
    
    // Check Steam installation
    QSettings steamRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam", QSettings::NativeFormat);
    QString steamPath = steamRegistry.value("InstallPath").toString();
    if (!steamPath.isEmpty()) {
        std::filesystem::path steamGamePath = std::filesystem::path(steamPath.toStdString()) / 
                                           "steamapps" / "common" / "Fallout 2";
        std::filesystem::path steamDataPath = steamGamePath / "data";
        if (std::filesystem::exists(steamDataPath)) {
            installations.push_back({steamGamePath, GameInstallationType::STEAM, "Steam Installation"});
            spdlog::info("Found Steam Fallout 2 installation: {}", steamGamePath.string());
        }
    }
    
#elif defined(__APPLE__)
    // macOS common paths
    std::vector<std::pair<std::filesystem::path, std::string>> macPaths = {
        {"/Applications/Fallout 2.app", "macOS App Bundle"},
        {"/Applications/Fallout 2.app/Contents/Resources", "macOS App Bundle (Resources)"},
        {std::filesystem::path(QDir::homePath().toStdString()) / "Applications/Fallout 2.app", "User App Bundle"}
    };
    
    for (const auto& [path, description] : macPaths) {
        if (std::filesystem::exists(path)) {
            // Check if it's an app bundle or contains data
            if (path.extension() == ".app") {
                installations.push_back({path, GameInstallationType::EXECUTABLE, description});
                spdlog::info("Found macOS Fallout 2 installation: {}", path.string());
            } else {
                std::filesystem::path dataPath = path / "data";
                if (std::filesystem::exists(dataPath)) {
                    installations.push_back({path, GameInstallationType::EXECUTABLE, description});
                    spdlog::info("Found macOS Fallout 2 installation: {}", path.string());
                }
            }
        }
    }
    
    // Check for Steam on macOS
    std::filesystem::path steamMacPath = std::filesystem::path(QDir::homePath().toStdString()) / 
                                        "Library/Application Support/Steam/steamapps/common/Fallout 2";
    if (std::filesystem::exists(steamMacPath / "data")) {
        installations.push_back({steamMacPath, GameInstallationType::STEAM, "Steam Installation (macOS)"});
        spdlog::info("Found Steam Fallout 2 installation on macOS: {}", steamMacPath.string());
    }
    
#else
    // Linux common paths
    std::vector<std::pair<std::filesystem::path, std::string>> linuxPaths = {
        {std::filesystem::path(QDir::homePath().toStdString()) / ".local/share/Steam/steamapps/common/Fallout 2", "Steam Installation"},
        {"/usr/local/games/fallout2", "System Installation"},
        {"/opt/fallout2", "System Installation"}
    };
    
    for (const auto& [path, description] : linuxPaths) {
        std::filesystem::path dataPath = path / "data";
        if (std::filesystem::exists(dataPath)) {
            GameInstallationType type = description.find("Steam") != std::string::npos ? 
                                      GameInstallationType::STEAM : GameInstallationType::EXECUTABLE;
            installations.push_back({path, type, description});
            spdlog::info("Found Linux Fallout 2 installation: {}", path.string());
        }
    }
#endif
    
    // Steam library detection (cross-platform)
    auto steamLibraries = detectSteamLibraries();
    for (const auto& library : steamLibraries) {
        std::filesystem::path fo2Path = library / "steamapps" / "common" / "Fallout 2";
        if (std::filesystem::exists(fo2Path / "data")) {
            installations.push_back({fo2Path, GameInstallationType::STEAM, "Steam Library Installation"});
            spdlog::info("Found Fallout 2 in Steam library: {}", fo2Path.string());
        }
    }
    
    return installations;
}

// Game location configuration
std::filesystem::path Settings::getGameLocation() const {
    // For Steam installations, we don't use a path - return empty path
    // The launcher should check the installation type and use Steam App ID instead
    if (_gameInstallationType == GameInstallationType::STEAM) {
        return std::filesystem::path{}; // Empty path for Steam
    } else {
        // Return the data directory for map copying and ddraw.ini modification
        return _gameDataDirectory.empty() ? _executableGameLocation : _gameDataDirectory;
    }
}

void Settings::setGameLocation(const std::filesystem::path& location) {
    _gameLocation = location; // Keep for backward compatibility
    if (_gameInstallationType == GameInstallationType::EXECUTABLE) {
        _executableGameLocation = location;
    }
    // For Steam installations, we don't store paths - only the App ID matters
    spdlog::info("Game location set to: {}", location.string());
}

Settings::GameInstallationType Settings::getGameInstallationType() const {
    return _gameInstallationType;
}

void Settings::setGameInstallationType(GameInstallationType type) {
    _gameInstallationType = type;
    spdlog::info("Game installation type set to: {}", type == GameInstallationType::STEAM ? "Steam" : "Executable");
}

std::string Settings::getSteamAppId() const {
    return _steamAppId;
}

void Settings::setSteamAppId(const std::string& appId) {
    _steamAppId = appId;
    spdlog::info("Steam App ID set to: {}", appId);
}

std::filesystem::path Settings::getExecutableGameLocation() const {
    return _executableGameLocation;
}

void Settings::setExecutableGameLocation(const std::filesystem::path& location) {
    _executableGameLocation = location;
    spdlog::info("Executable game location set to: {}", location.string());
}

std::filesystem::path Settings::getGameDataDirectory() const {
    return _gameDataDirectory;
}

void Settings::setGameDataDirectory(const std::filesystem::path& location) {
    _gameDataDirectory = location;
    spdlog::info("Game data directory set to: {}", location.string());
}

bool Settings::isGameLocationValid() const {
    if (_gameLocation.empty()) {
        return false;
    }
    
    // Check if directory exists
    if (!std::filesystem::exists(_gameLocation) || !std::filesystem::is_directory(_gameLocation)) {
        return false;
    }
    
    // Check for essential Fallout 2 files/directories
    std::filesystem::path dataDir = _gameLocation / "data";
    
#ifdef __APPLE__
    // macOS: Check for .app bundles or Steam installation
    std::filesystem::path fallout2App = _gameLocation / "Fallout 2.app";
    std::filesystem::path fallout2App2 = _gameLocation / "fallout2.app";
    
    // Also check global locations
    std::filesystem::path globalApp1 = "/Applications/Fallout 2.app";
    std::filesystem::path globalApp2 = "/Applications/fallout2.app";
    
    bool hasDataDir = std::filesystem::exists(dataDir) && std::filesystem::is_directory(dataDir);
    bool hasApp = std::filesystem::exists(fallout2App) || std::filesystem::exists(fallout2App2) ||
                  std::filesystem::exists(globalApp1) || std::filesystem::exists(globalApp2);
    
    return hasDataDir || hasApp;
#else
    // Windows/Linux: Check for executable files
    std::filesystem::path executable = _gameLocation / "fallout2.exe";
    std::filesystem::path executable2 = _gameLocation / "Fallout2.exe";
    std::filesystem::path executable3 = _gameLocation / "fallout2";
    std::filesystem::path executable4 = _gameLocation / "Fallout2";
    
    bool hasDataDir = std::filesystem::exists(dataDir) && std::filesystem::is_directory(dataDir);
    bool hasExecutable = std::filesystem::exists(executable) || std::filesystem::exists(executable2) ||
                        std::filesystem::exists(executable3) || std::filesystem::exists(executable4);
    
    return hasDataDir && hasExecutable;
#endif
}

void Settings::autoDetectGameLocation() {
    auto installations = detectFallout2Installations();
    
    if (!installations.empty()) {
        // Get parent directory of data folder (the game installation directory)
        std::filesystem::path gameDir = installations[0].parent_path();
        setGameLocation(gameDir);
        spdlog::info("Auto-detected game location: {}", gameDir.string());
    } else {
        spdlog::warn("No Fallout 2 installations detected");
    }
}

} // namespace geck