#define QT_NO_EMIT
#include "Application.h"

#include <algorithm>

#include <spdlog/spdlog.h>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>
#include <QIcon>
#include <QCoreApplication>

#include "version.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "ui/logging/LogModel.h"
#include "ui/logging/LogModelSink.h"
#include "state/loader/MapLoader.h"
#include "util/GameDataPathResolver.h"
#include "ui/Settings.h"
#include "ui/QtDialogs.h"
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

    // Mirror every log record into the Log panel's model from here on, so load-time warnings
    // (missing tile art, unresolved sprites, ...) reach the UI, not just the console.
    _logModel = std::make_unique<LogModel>();
    _logSink = std::make_shared<LogModelSink>(_logModel.get());
    spdlog::default_logger()->sinks().push_back(_logSink);

    std::filesystem::path iconPath = getResourcesPath() / "icon.png";
    QIcon appIcon(QString::fromStdString(iconPath.string()));
    _qtApp->setWindowIcon(appIcon);

    const std::string finalMapPath = processCommandLineArgs();

    initUI();

    checkDataConfiguration();

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

        // Expand a folder into the folder + its master.dat/critter.dat so the DATs are explicit,
        // mounted entries (DataFileSystem no longer nested-mounts them). Add to settings, don't save/load yet.
        for (const auto& entry : util::expandDataPaths({ std::filesystem::path(dataPath.toStdString()) })) {
            settings.addDataPath(entry);
        }
    }

    // Data paths will be loaded after settings dialog in checkFirstRun()
    return parser.isSet(mapOption) ? parser.value(mapOption).toStdString() : "";
}

Application::~Application() {
    if (_logSink) {
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), _logSink), sinks.end());
        _logSink->detach();
    }
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
    _mainWindow->setLogModel(_logModel.get());

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

void Application::checkDataConfiguration() {
    auto& settings = *_settings;
    if (!settings.exists()) {
        spdlog::info("First run detected, showing settings dialog");
        showStartupSettingsDialog();
        loadDataPaths();
        return;
    }

    loadDataPaths();

    // Settings exist, but the configured paths no longer provide the files the editor can't
    // run without (the game install moved or was deleted since the last run). Prompt once with
    // the same dialog as on first run, then remount whatever the user configured.
    if (!hasEssentialGameData()) {
        spdlog::warn("Configured data paths are missing essential game files, showing settings dialog");
        if (showStartupSettingsDialog()) {
            _resources->clearAllDataPaths();
            loadDataPaths();
        }
    }
}

bool Application::showStartupSettingsDialog() {
    SettingsDialog dialog(_settings, _mainWindow.get());

    bool dataPathsChanged = false;
    QObject::connect(&dialog, &SettingsDialog::settingsSaved, [&dataPathsChanged](bool changed) {
        dataPathsChanged = dataPathsChanged || changed;
    });

    dialog.exec();

    // Save whether or not the dialog was accepted, so we keep at least the default data path
    // from the command line and the app is usable either way.
    _settings->save();
    return dataPathsChanged;
}

bool Application::hasEssentialGameData() const {
    // The palette and the tile list back every rendering path; if the mounted data paths can't
    // resolve them, no map can be displayed and the data configuration needs fixing.
    const auto& files = _resources->files();
    return files.exists(ResourcePaths::Pal::COLOR) && files.exists(ResourcePaths::Lst::TILES);
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

    spdlog::debug("Data paths loaded successfully");
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
