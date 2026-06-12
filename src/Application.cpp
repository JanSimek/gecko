#define QT_NO_EMIT
#include "Application.h"

#include <spdlog/spdlog.h>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>
#include <QIcon>
#include <QCoreApplication>

#include "version.h"
#include "resource/GameResources.h"
#include "state/loader/MapLoader.h"
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
    , _settings(std::make_shared<Settings>())
    , _mainWindow(nullptr)
    , _resources(std::make_shared<resource::GameResources>()) {

    _qtApp->setApplicationName(geck::version::name);
    _qtApp->setApplicationDisplayName(geck::version::name);
    _qtApp->setApplicationVersion(geck::version::string);

    std::filesystem::path iconPath = getResourcesPath() / "icon.png";
    QIcon appIcon(QString::fromStdString(iconPath.string()));
    _qtApp->setWindowIcon(appIcon);

    const std::string finalMapPath = processCommandLineArgs();

    initUI();

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

    // MapLoader is Qt-free; LoadingWidget owns it once added, so the callback uses
    // a handle to read its error message and present it here on the main thread.
    auto loaderHandle = std::make_shared<MapLoader*>(nullptr);
    auto mapLoader = std::make_unique<MapLoader>(_resources, mapPath, -1, true, [this, loaderHandle](auto map) {
        if (map) {
            auto editorWidget = std::make_unique<EditorWidget>(*_resources, std::move(map));
            _mainWindow->setEditorWidget(std::move(editorWidget));
        } else if (*loaderHandle && (*loaderHandle)->hasError()) {
            QtDialogs::showError(_mainWindow.get(), "Missing Game Files",
                QString::fromStdString((*loaderHandle)->errorMessage()));
        }
    });
    *loaderHandle = mapLoader.get();
    loadingWidget->addLoader(std::move(mapLoader));

    loadingWidget->exec();
}

std::string Application::processCommandLineArgs() {
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.setApplicationDescription(geck::version::description);

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

    auto& settings = *_settings;
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
    if (_resources) {
        _resources->clearCaches();
    }
}

void Application::initUI() {
    _mainWindow = std::make_unique<MainWindow>(_resources, _settings);

    // Check if this is first run or if user prefers maximized
    auto& settings = *_settings;
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
    auto& settings = *_settings;
    if (!settings.exists()) {
        spdlog::info("First run detected, showing settings dialog");

        SettingsDialog dialog(_settings, _mainWindow.get());
        int result = dialog.exec();

        // Save even if cancelled so we keep at least the default data path from the command line.
        settings.save();

        if (result == QDialog::Accepted) {
            spdlog::info("Settings dialog accepted, configuration saved");
            loadDataPaths();
        } else {
            spdlog::info("Settings dialog cancelled, saving default configuration");
            // Load data paths even if cancelled, so the app is usable.
            loadDataPaths();
        }
    } else {
        loadDataPaths();
    }
}

void Application::loadDataPaths() {
    auto& settings = *_settings;
    auto dataPaths = settings.getDataPaths();

    if (dataPaths.empty()) {
        spdlog::warn("No data paths configured, application may not function properly");
        return;
    }

    spdlog::info("Loading {} data paths with progress dialog", dataPaths.size());

    // Load game data even without a map: GameResources, the file browser, and new-map
    // creation all need access to FRM/tile/object assets from the DAT files.
    auto loadingWidget = std::make_unique<LoadingWidget>(_mainWindow.get());
    loadingWidget->setWindowTitle("Loading Game Data");
    loadingWidget->addLoader(std::make_unique<DataPathLoader>(_resources, dataPaths));

    loadingWidget->exec();

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
        // Inside a bundle, resources live in ../Resources
        std::filesystem::path bundlePath = appPath.toStdString();
        return bundlePath.parent_path() / "Resources" / RESOURCES_DIR;
    } else {
        return std::filesystem::current_path() / RESOURCES_DIR;
    }
#else
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
