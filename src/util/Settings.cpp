#include "Settings.h"
#include "GameDataPathResolver.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonParseError>
#include <QDebug>
#include <spdlog/spdlog.h>

#include <algorithm>

#ifdef _WIN32
#include <QSettings>
#include <windows.h>
#endif

namespace geck {

namespace {

    template <typename T>
    void appendUnique(std::vector<T>& values, T value) {
        if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.push_back(std::move(value));
        }
    }

}

Settings::Settings()
    : _version(SETTINGS_VERSION) {
}

Settings& Settings::getInstance() {
    static Settings instance;
    return instance;
}

QString Settings::getSettingsFilePath() const {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir configDir;

    if (!configDir.mkpath(configPath)) {
        return QDir(configPath).filePath(SETTINGS_FILENAME);
    }

    const QString organizationPath = QDir(configPath).filePath(ORGANIZATION_NAME);
    if (!configDir.mkpath(organizationPath)) {
        return QDir(configPath).filePath(SETTINGS_FILENAME);
    }

    return QDir(organizationPath).filePath(SETTINGS_FILENAME);
}

bool Settings::exists() const {
    return QFile::exists(getSettingsFilePath());
}

void Settings::load() {
    QString filePath = getSettingsFilePath();
    spdlog::debug("Loading settings from configuration path: {}", filePath.toStdString());

    if (!QFile::exists(filePath)) {
        spdlog::info("Settings file not found in {}, using defaults", filePath.toStdString());
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

    if (!_panelVisibilityPreferences.isEmpty()) {
        QJsonObject panelVisibility;
        for (auto it = _panelVisibilityPreferences.begin(); it != _panelVisibilityPreferences.end(); ++it) {
            panelVisibility[it.key()] = it.value();
        }
        ui["panelVisibility"] = panelVisibility;
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

    json["gameLocation"] = gameLocation;

    return json;
}

void Settings::fromJson(const QJsonObject& json) {
    _version = json["version"].toString(SETTINGS_VERSION);

    // Data paths
    if (json.contains("dataPaths")) {
        setDataPaths(jsonArrayToPathVector(json["dataPaths"].toArray()));
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

        if (ui.contains("panelVisibility")) {
            QJsonObject panelVisibility = ui["panelVisibility"].toObject();
            _panelVisibilityPreferences.clear();
            for (auto it = panelVisibility.begin(); it != panelVisibility.end(); ++it) {
                _panelVisibilityPreferences[it.key()] = it.value().toBool(true);
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
    if (json.contains("gameLocation") && json["gameLocation"].isObject()) {
        QJsonObject gameLocation = json["gameLocation"].toObject();

        QString installationType = gameLocation["installationType"].toString("executable");
        _gameInstallationType = (installationType == "steam") ? GameInstallationType::STEAM : GameInstallationType::EXECUTABLE;

        if (gameLocation.contains("steamAppId")) {
            QString steamAppId = gameLocation["steamAppId"].toString();
            if (!steamAppId.isEmpty()) {
                _steamAppId = steamAppId.toStdString();
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
    const std::filesystem::path normalizedPath = normalizeDataPath(path);

    // Check if path already exists
    for (const auto& existingPath : _dataPaths) {
        if (util::pathsEquivalent(existingPath, normalizedPath)) {
            spdlog::debug("Data path already exists: {}", normalizedPath.string());
            return;
        }
    }

    _dataPaths.push_back(normalizedPath);
    spdlog::info("Added data path: {}", normalizedPath.string());
}

void Settings::removeDataPath(const std::filesystem::path& path) {
    const std::filesystem::path normalizedPath = normalizeDataPath(path);
    auto it = std::remove_if(_dataPaths.begin(), _dataPaths.end(),
        [&normalizedPath](const std::filesystem::path& existing) {
            return util::pathsEquivalent(existing, normalizedPath);
        });

    if (it != _dataPaths.end()) {
        _dataPaths.erase(it, _dataPaths.end());
        spdlog::info("Removed data path: {}", normalizedPath.string());
    }
}

std::vector<std::filesystem::path> Settings::getDataPaths() const {
    return _dataPaths;
}

void Settings::setDataPaths(const std::vector<std::filesystem::path>& paths) {
    _dataPaths.clear();
    for (const auto& path : paths) {
        addDataPath(path);
    }
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

std::optional<bool> Settings::getPanelVisibilityPreference(const QString& panelName) const {
    auto it = _panelVisibilityPreferences.find(panelName);
    if (it != _panelVisibilityPreferences.end()) {
        return it.value();
    }
    return std::nullopt;
}

void Settings::setPanelVisibilityPreference(const QString& panelName, bool visible) {
    _panelVisibilityPreferences[panelName] = visible;
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

bool Settings::validateDataPath(const std::filesystem::path& path) const {
    const std::filesystem::path normalizedPath = normalizeDataPath(path);
    std::error_code ec;

    if (!std::filesystem::exists(normalizedPath, ec)) {
        return false;
    }

    if (std::filesystem::is_directory(normalizedPath, ec)) {
        return true;
    }

    if (normalizedPath.extension() == ".dat" && std::filesystem::is_regular_file(normalizedPath, ec)) {
        return true;
    }

    return false;
}

std::filesystem::path Settings::normalizeDataPath(const std::filesystem::path& path) {
    if (path.extension() == ".dat") {
        return path;
    }
    return util::resolveGameDataRoot(path).value_or(path);
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
        const std::filesystem::path gamePath(gogPath.toStdString());
        if (const auto normalized = util::resolveGameDataRoot(gamePath)) {
            appendUnique(installations, *normalized);
            spdlog::info("Found GOG Fallout 2 installation: {}", normalized->string());
        }
    }

    // Check Steam installation
    QSettings steamRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam", QSettings::NativeFormat);
    QString steamPath = steamRegistry.value("InstallPath").toString();
    if (!steamPath.isEmpty()) {
        const std::filesystem::path steamFo2Path = std::filesystem::path(steamPath.toStdString()) / "steamapps" / "common" / "Fallout 2";
        if (const auto normalized = util::resolveGameDataRoot(steamFo2Path)) {
            appendUnique(installations, *normalized);
            spdlog::info("Found Steam Fallout 2 installation: {}", normalized->string());
        }
    }

#elif defined(__APPLE__)
    // macOS common paths
    std::vector<std::filesystem::path> macPaths = {
        "/Applications/Fallout 2.app",
        "/Applications/Fallout 2.app/Contents/Resources",
        std::filesystem::path(QDir::homePath().toStdString()) / "Applications/Fallout 2.app/Contents/Resources"
    };

    for (const auto& path : macPaths) {
        if (const auto normalized = util::resolveGameDataRoot(path)) {
            appendUnique(installations, *normalized);
            spdlog::info("Found macOS Fallout 2 installation: {}", normalized->string());
        }
    }

#else
    // Linux common paths
    std::vector<std::filesystem::path> linuxPaths = {
        std::filesystem::path(QDir::homePath().toStdString()) / ".local/share/Steam/steamapps/common/Fallout 2",
        "/usr/local/games/fallout2",
        "/opt/fallout2"
    };

    for (const auto& path : linuxPaths) {
        if (const auto normalized = util::resolveGameDataRoot(path)) {
            appendUnique(installations, *normalized);
            spdlog::info("Found Linux Fallout 2 installation: {}", normalized->string());
        }
    }
#endif

    // Steam library detection (cross-platform)
    auto steamLibraries = detectSteamLibraries();
    for (const auto& library : steamLibraries) {
        const std::filesystem::path fo2Path = library / "steamapps" / "common" / "Fallout 2";
        if (const auto normalized = util::resolveGameDataRoot(fo2Path)) {
            appendUnique(installations, *normalized);
            spdlog::info("Found Fallout 2 in Steam library: {}", normalized->string());
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
        if (util::hasFallout2DataLayout(gogGamePath)) {
            installations.push_back({ gogGamePath, GameInstallationType::EXECUTABLE, "GOG Installation" });
            spdlog::info("Found GOG Fallout 2 installation: {}", gogGamePath.string());
        }
    }

    // Check Steam installation
    QSettings steamRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam", QSettings::NativeFormat);
    QString steamPath = steamRegistry.value("InstallPath").toString();
    if (!steamPath.isEmpty()) {
        std::filesystem::path steamGamePath = std::filesystem::path(steamPath.toStdString()) / "steamapps" / "common" / "Fallout 2";
        if (util::hasFallout2DataLayout(steamGamePath)) {
            installations.push_back({ steamGamePath, GameInstallationType::STEAM, "Steam Installation" });
            spdlog::info("Found Steam Fallout 2 installation: {}", steamGamePath.string());
        }
    }

#elif defined(__APPLE__)
    // macOS common paths
    std::vector<std::pair<std::filesystem::path, std::string>> macPaths = {
        { "/Applications/Fallout 2.app", "macOS App Bundle" },
        { std::filesystem::path(QDir::homePath().toStdString()) / "Applications/Fallout 2.app", "User App Bundle" }
    };

    for (const auto& [path, description] : macPaths) {
        if (util::resolveGameDataRoot(path).has_value()) {
            installations.push_back({ path, GameInstallationType::EXECUTABLE, description });
            spdlog::info("Found macOS Fallout 2 installation: {}", path.string());
        }
    }

    // Check for Steam on macOS
    std::filesystem::path steamMacPath = std::filesystem::path(QDir::homePath().toStdString()) / "Library/Application Support/Steam/steamapps/common/Fallout 2";
    if (util::hasFallout2DataLayout(steamMacPath)) {
        installations.push_back({ steamMacPath, GameInstallationType::STEAM, "Steam Installation (macOS)" });
        spdlog::info("Found Steam Fallout 2 installation on macOS: {}", steamMacPath.string());
    }

#else
    // Linux common paths
    std::vector<std::pair<std::filesystem::path, std::string>> linuxPaths = {
        { std::filesystem::path(QDir::homePath().toStdString()) / ".local/share/Steam/steamapps/common/Fallout 2", "Steam Installation" },
        { "/usr/local/games/fallout2", "System Installation" },
        { "/opt/fallout2", "System Installation" }
    };

    for (const auto& [path, description] : linuxPaths) {
        if (util::hasFallout2DataLayout(path)) {
            GameInstallationType type = description.find("Steam") != std::string::npos ? GameInstallationType::STEAM : GameInstallationType::EXECUTABLE;
            installations.push_back({ path, type, description });
            spdlog::info("Found Linux Fallout 2 installation: {}", path.string());
        }
    }
#endif

    // Steam library detection (cross-platform)
    auto steamLibraries = detectSteamLibraries();
    for (const auto& library : steamLibraries) {
        std::filesystem::path fo2Path = library / "steamapps" / "common" / "Fallout 2";
        if (util::hasFallout2DataLayout(fo2Path)) {
            installations.push_back({ fo2Path, GameInstallationType::STEAM, "Steam Library Installation" });
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
        if (!_gameDataDirectory.empty()) {
            return _gameDataDirectory;
        }

        // Use executable's parent directory as the data directory
        return _executableGameLocation.parent_path();
    }
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
    if (_gameInstallationType == GameInstallationType::STEAM) {
        return !_steamAppId.empty();
    }

    if (_executableGameLocation.empty()) {
        return false;
    }

    if (!std::filesystem::exists(_executableGameLocation)) {
        return false;
    }

    // Get the data directory (executable should be a file, use its parent directory as fallback)
    std::filesystem::path dataRoot = _gameDataDirectory.empty() ? _executableGameLocation.parent_path() : _gameDataDirectory;

    if (!std::filesystem::exists(dataRoot) || !std::filesystem::is_directory(dataRoot)) {
        return false;
    }

    // Check if data/maps directory exists (required for map copying)
    std::filesystem::path dataDir = dataRoot / "data";
    return std::filesystem::exists(dataDir) && std::filesystem::is_directory(dataDir);
}

void Settings::autoDetectGameLocation() {
    auto installations = detectFallout2Installations();

    if (!installations.empty()) {
        const std::filesystem::path& gameDir = installations[0];
        _gameInstallationType = GameInstallationType::EXECUTABLE;
        _executableGameLocation = gameDir;
        _gameDataDirectory = gameDir;
        spdlog::info("Auto-detected game location: {}", gameDir.string());
    } else {
        spdlog::warn("No Fallout 2 installations detected");
    }
}

} // namespace geck
