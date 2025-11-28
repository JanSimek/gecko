#define QT_NO_EMIT
#include "Application.h"

#include <spdlog/spdlog.h>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>
#include <QIcon>
#include <QCoreApplication>

#include "version.h"
#include "state/loader/MapLoader.h"
#include "util/ResourceManager.h"
#include "util/Settings.h"
#include "util/QtDialogs.h"
#include "ui/core/MainWindow.h"
#include "ui/core/EditorWidget.h"
#include "ui/widgets/LoadingWidget.h"
#include "ui/dialogs/SettingsDialog.h"
#include "state/loader/DataPathLoader.h"
#include "ui/panels/FileBrowserPanel.h"

namespace geck {

Application::Application(int argc, char** argv)
    : _qtApp(std::make_unique<QApplication>(argc, argv))
    , _mainWindow(nullptr) {

    _qtApp->setApplicationName(geck::version::name);
    _qtApp->setApplicationDisplayName(geck::version::name);
    _qtApp->setApplicationVersion(geck::version::string);

    std::filesystem::path iconPath = getResourcesPath() / "icon.png";
    QIcon appIcon(QString::fromStdString(iconPath.string()));
    _qtApp->setWindowIcon(appIcon);

    const std::string finalMapPath = processCommandLineArgs();

    initUI();
    
    // Check for first run and show settings dialog if needed
    checkFirstRun();

    loadMap(finalMapPath);
}

void Application::loadMap(const std::filesystem::path& mapPath) {
    if (mapPath.empty()) {
        spdlog::info("No map file specified, starting with empty editor");
        return;
    }

    auto loadingWidget = std::make_unique<LoadingWidget>(_mainWindow.get());
    loadingWidget->setWindowTitle("Loading Map");

    // Add map loader (filesystem loading for command line args)
    loadingWidget->addLoader(std::make_unique<MapLoader>(mapPath, -1, true, [this](auto map) {
        // Check if loading was successful
        if (map) {
            // When loading is complete, create editor widget and switch to it
            auto editorWidget = std::make_unique<EditorWidget>(std::move(map));
            _mainWindow->setEditorWidget(std::move(editorWidget));
        }
        // If map is null, error was already shown by MapLoader::onDone()
    }));

    // Show modal loading dialog
    loadingWidget->exec();
}

std::string Application::processCommandLineArgs() {
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.setApplicationDescription(geck::version::description);

    // Determine the default resources path using the centralized method
    std::filesystem::path default_resources_path = getResourcesPath();

    QCommandLineOption dataOption(QStringList() << "d" << "data",
        "Path to the Fallout 2 directory or individual data files, e.g. master.dat and critter.dat",
        "path", QString::fromStdString(default_resources_path.string()));
    parser.addOption(dataOption);

    QCommandLineOption mapOption(QStringList() << "m" << "map",
        "Path to the map file to load",
        "mapfile");
    parser.addOption(mapOption);

    QCommandLineOption debugOption("debug", "Show debug messages");
    parser.addOption(debugOption);

    parser.process(*_qtApp);

    if (parser.isSet(debugOption)) {
        spdlog::set_pattern("[%^%l%$] [thread %t] %v");
        spdlog::set_level(spdlog::level::debug);
    }

    auto& settings = Settings::getInstance();
    bool isFirstRun = !settings.exists();

    if (!isFirstRun) {
        settings.load();
    }
    
    // For first run, we'll add the default path but won't load it yet
    if (settings.getDataPaths().empty()) {
        QString dataPath = parser.value(dataOption);
        spdlog::info("No data paths in settings, will use command line default: {}", dataPath.toStdString());
        
        // Add to settings but don't save or load yet
        settings.addDataPath(dataPath.toStdString());
    }
    
    // Data paths will be loaded after settings dialog in checkFirstRun()
    return parser.isSet(mapOption) ? parser.value(mapOption).toStdString() : "";
}

Application::~Application() {
    if (_mainWindow) {
        _mainWindow->stopGameLoop();
    }
    // OpenGL textures must be destroyed while the OpenGL context is still valid;
    // without this we get mutex/context crash during static destruction
    ResourceManager::getInstance().cleanup();
}

void Application::initUI() {
    _mainWindow = std::make_unique<MainWindow>();
    
    // Check if this is first run or if user prefers maximized
    auto& settings = Settings::getInstance();
    if (!settings.exists() || settings.getWindowMaximized()) {
        _mainWindow->showMaximized();
    } else {
        _mainWindow->show();
    }
}

void Application::run() {
    _mainWindow->startGameLoop();

    int result = _qtApp->exec();
    spdlog::debug("Application exited with code: {}", result);
}

bool Application::isRunning() const {
    return _mainWindow && _mainWindow->isVisible();
}

void Application::checkFirstRun() {
    auto& settings = Settings::getInstance();
    if (!settings.exists()) {
        spdlog::info("First run detected, showing settings dialog");
        
        SettingsDialog dialog(_mainWindow.get());
        int result = dialog.exec();
        
        // Always save settings after first run, even if cancelled
        // This ensures we have at least the default data path from command line
        settings.save();
        
        if (result == QDialog::Accepted) {
            spdlog::info("Settings dialog accepted, configuration saved");
            loadDataPaths();
        } else {
            spdlog::info("Settings dialog cancelled, saving default configuration");
            // Still load data paths even if cancelled, so the app is usable
            loadDataPaths();
        }
    } else {
        // Not first run, load data paths normally
        loadDataPaths();
    }
}

void Application::loadDataPaths() {
    auto& settings = Settings::getInstance();
    auto dataPaths = settings.getDataPaths();
    
    if (dataPaths.empty()) {
        spdlog::warn("No data paths configured, application may not function properly");
        return;
    }
    
    spdlog::info("Loading {} data paths with progress dialog", dataPaths.size());
    
    // Load Fallout 2 game data files (DAT files, directories) even when no map is loaded
    // This is essential because:
    // 1. ResourceManager needs access to game assets (textures, sprites, sounds)
    // 2. File browser requires loaded data to display available maps and resources
    // 3. Creating new maps needs tile/object assets from game data
    // 4. Editor cannot function properly without access to FRM files and other resources
    auto loadingWidget = std::make_unique<LoadingWidget>(_mainWindow.get());
    loadingWidget->setWindowTitle("Loading Game Data");
    loadingWidget->addLoader(std::make_unique<DataPathLoader>(dataPaths));
    
    // Show modal loading dialog - this appears even without a map loaded
    loadingWidget->exec();
    
    // After data loading completes, refresh the file browser so it shows the loaded files
    if (_mainWindow) {
        _mainWindow->refreshFileBrowser();
        _mainWindow->showFileBrowserPanel();
    }
    
    spdlog::info("Data paths loaded successfully");
}

std::filesystem::path Application::getResourcesPath() {
#ifdef __APPLE__
    // Check if we're running from a macOS app bundle
    QString appPath = QCoreApplication::applicationDirPath();
    if (appPath.contains(".app/Contents/MacOS")) {
        // We're in a bundle, resources are in ../Resources
        std::filesystem::path bundlePath = appPath.toStdString();
        return bundlePath.parent_path() / "Resources" / RESOURCES_DIR;
    } else {
        // Not in a bundle, use current directory
        return std::filesystem::current_path() / RESOURCES_DIR;
    }
#else
    // For Windows and Linux, use current directory
    return std::filesystem::current_path() / RESOURCES_DIR;
#endif
}

bool Application::isDefaultResourcesPath(const std::filesystem::path& path) {
    try {
        std::filesystem::path defaultPath = getResourcesPath();
        return std::filesystem::equivalent(path, defaultPath);
    } catch (const std::filesystem::filesystem_error&) {
        // If we can't compare paths (e.g., one doesn't exist), compare strings
        return path == getResourcesPath();
    }
}

} // namespace geck
