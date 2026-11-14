#define QT_NO_EMIT
#include "Application.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>
#include <QIcon>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

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

#include <clocale>

namespace geck {

namespace {

    // Qt adopts the system locale (setlocale(LC_ALL, "")) as the application starts. A decimal-comma
    // LC_NUMERIC (cs_CZ, de_DE, ...) then breaks C-level number parsing: Luau rejected `1.0` in the
    // script console as a malformed number. Keep numbers in the C locale, before anything parses.
    std::unique_ptr<QApplication> createQApplication(int& argc, char** argv) {
        auto app = std::make_unique<QApplication>(argc, argv);
        std::setlocale(LC_NUMERIC, "C");
        return app;
    }

} // namespace

Application::Application(int argc, char** argv)
    : _qtApp(createQApplication(argc, argv))
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

    // Also persist the log to a rotating file: a Finder-launched app has no visible console,
    // and slow-start reports need the timings from the exact run that misbehaved.
    try {
        const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
        QDir().mkpath(logDir);
        const std::string logFile = (logDir + "/gecko.log").toStdString();
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFile, 2 * 1024 * 1024, 2);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::default_logger()->sinks().push_back(std::move(fileSink));
        // Flush every record: this file exists to explain hangs and kills, so it must be
        // current even when the process never exits cleanly.
        spdlog::default_logger()->flush_on(spdlog::level::info);
        spdlog::info("Logging to {}", logFile);
    } catch (const std::exception& e) {
        spdlog::warn("Could not open the log file: {}", e.what());
    }

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
    auto mapLoader = std::make_unique<MapLoader>(_resources, mapPath, true, [this, loaderHandle](auto map) {
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

    if (parser.isSet(dataOption)) {
        _cliDataPathOverride = std::filesystem::path(parser.value(dataOption).toStdString());
    }

    if (parser.isSet(debugOption)) {
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");
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
        // mounted entries (DataFileSystem does not nested-mount them). Add to settings, don't save/load yet.
        for (const auto& entry : util::expandDataPaths({ std::filesystem::path(dataPath.toStdString()) })) {
            settings.addDataPath(entry);
        }
    }

    // Data paths will be loaded after settings dialog in checkFirstRun()
    return parser.isSet(mapOption) ? parser.value(mapOption).toStdString() : "";
}

Application::~Application() {
    if (_logSink) {
        // Detach only — do not mutate the logger's sink list here: a worker thread still logging
        // would race the unsynchronized vector. The detached sink stays installed but inert
        // (detach and sink_it_ hold the same base_sink mutex) until spdlog tears down at exit.
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

namespace {

// Shared between loadDataPaths() (to refuse mounting it) and showStartupSettingsDialog()
// (to refuse PERSISTING it) - a real, confirmed bug: the dialog's own save used to run
// unconditionally after dialog.exec(), so whenever this exact placeholder shape was already
// sitting in memory when the recovery dialog opened, closing that dialog wrote it straight
// back to disk regardless of what the user did - permanently locking in a bad config that
// would otherwise have been transient. Every occurrence traced so far shows the SAME frozen
// mtime from the moment this first got saved, never updating again on its own.
bool isPlaceholderOnlyDataPaths(const std::vector<std::filesystem::path>& dataPaths) {
    if (dataPaths.empty()) {
        return false;
    }
    const std::string resourcesPrefix = [] {
        std::string s = Application::getResourcesPath().lexically_normal().string();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }();
    const bool allUnderResources = !resourcesPrefix.empty()
        && std::all_of(dataPaths.begin(), dataPaths.end(), [&resourcesPrefix](const std::filesystem::path& p) {
               std::string s = p.lexically_normal().string();
               std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
               return s.rfind(resourcesPrefix, 0) == 0; // starts_with
           });
    return allUnderResources && dataPaths.size() <= 3;
}

} // namespace

void Application::checkDataConfiguration() {
    auto& settings = *_settings;
    if (!settings.exists()) {
        spdlog::info("First run detected, showing settings dialog");
        showStartupSettingsDialog();
        loadDataPaths();
        _mainWindow->startThumbnailPrewarm();
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

    _mainWindow->startThumbnailPrewarm();
}

bool Application::showStartupSettingsDialog() {
    // The first-run dialog has no registry to offer (the window's is built with it, and rebinding
    // before any data path is configured is not the point of this dialog), so no shortcuts tab.
    SettingsDialog dialog(_settings, nullptr, _mainWindow.get());

    bool dataPathsChanged = false;
    QObject::connect(&dialog, &SettingsDialog::settingsSaved, [&dataPathsChanged](bool changed) {
        dataPathsChanged = dataPathsChanged || changed;
    });

    dialog.exec();

    // Save whether or not the dialog was accepted, so we keep at least the default data path
    // from the command line and the app is usable either way - UNLESS what's currently in
    // memory is the known placeholder-only pattern loadDataPaths() refuses to mount. Saving
    // that unconditionally was the actual bug behind data paths "permanently" resetting: this
    // dialog only ever opens because something is already wrong, so persisting whatever is
    // sitting in memory at that moment (rather than what the user actually configured) writes
    // the bad state right back to disk and locks it in for every future launch.
    if (isPlaceholderOnlyDataPaths(_settings->getDataPaths())) {
        spdlog::warn("showStartupSettingsDialog - not saving: in-memory data paths are still the "
                     "placeholder-only pattern; saving this would freeze it to disk permanently");
    } else {
        _settings->save();
    }
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

    // Sanity check BEFORE mounting anything: if every configured path resolves under the
    // bundled resources folder, this is the exact shape of a known, still-unexplained bug
    // where settings intermittently appear wrong to this process and gecko silently
    // substitutes its own bundled (vanilla-only) master.dat/critter.dat for the real
    // configured install - producing a plausible-looking but WRONG session with no visible
    // error (confirmed to have happened repeatedly). Confirmed via forensic capture: this isn't
    // a one-off flicker, it's this process reading a permanently-stuck stale snapshot of the
    // settings file that no in-app retry or resave can correct - the file this process sees
    // never changes no matter what gets written through any other path. Rather than just refuse,
    // fall back to an explicit --data argument when one was given: it comes from the command
    // line, not this unreliable file, so it sidesteps the problem entirely.
    if (isPlaceholderOnlyDataPaths(dataPaths)) {
        if (_cliDataPathOverride) {
            spdlog::warn("loadDataPaths - settings are the known placeholder-only pattern; "
                         "falling back to the --data argument instead of refusing: {}",
                _cliDataPathOverride->string());
            dataPaths = util::expandDataPaths({ *_cliDataPathOverride });
            // expandDataPaths only turns a bare game folder into itself + its master.dat/
            // critter.dat - it has no idea a mods/ subfolder (any mod, not just this one)
            // exists or needs to be mounted with higher priority than the base game. A settings
            // file built through the normal Settings dialog carries those separately; this
            // fallback has to rediscover them itself or a mod's own content silently vanishes
            // with no error at all, which is worse than the dialog this is trying to avoid.
            std::error_code ec;
            const auto modsDir = *_cliDataPathOverride / "mods";
            if (std::filesystem::is_directory(modsDir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(modsDir, ec)) {
                    if (entry.is_directory()) {
                        spdlog::info("loadDataPaths - fallback also mounting mod folder: {}",
                            entry.path().string());
                        dataPaths.push_back(entry.path());
                    }
                }
            }
            settings.setDataPaths(dataPaths);
        } else {
            spdlog::error("Refusing to load: all {} configured data path(s) resolve under the "
                          "bundled resources folder instead of a real game install, and no "
                          "--data argument was given to fall back to - not mounting.",
                dataPaths.size());
            QtDialogs::showError(_mainWindow.get(), "Data Configuration Problem",
                "gecko's data paths appear to have reset to its own bundled placeholder data "
                "instead of your real game install, and no --data argument was given to recover "
                "with.\n\nNothing has been loaded. Please close gecko and launch it with "
                "--data \"<path to your Fallout 2 install>\".");
            return;
        }
    }

    // The editor's own assets (blank tile, overlay art, ...) live in the bundled resources
    // folder, not in the game data — keep it mounted (lowest priority) regardless of how the
    // user configured the data paths, or every map load fails on art/tiles/blank.frm.
    util::ensureFallbackDataPath(dataPaths, getResourcesPath());

    spdlog::info("Loading {} data paths with progress dialog", dataPaths.size());
    for (const auto& path : dataPaths) {
        spdlog::info("  data path: {}", path.string());
    }

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
    // Always the EXECUTABLE's own directory, never the process's current working
    // directory. current_path() depends on how gecko happened to be launched (a
    // shortcut with an explicit "Start in", a taskbar pin, double-clicking the exe
    // from Explorer, a debugger) and is not guaranteed to equal the exe's folder -
    // when it doesn't, this path silently resolves to the wrong (or a nonexistent)
    // location, which breaks both the bundled-resources fallback mount AND the
    // loadDataPaths() safety check that string-compares configured paths against
    // this same prefix (a mismatch here means that check can't recognize the exact
    // bundled-placeholder pattern it exists to catch, and quietly lets it through).
    QString appPath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
    // Check if we're running from a macOS app bundle
    if (appPath.contains(".app/Contents/MacOS")) {
        // Inside a bundle, resources live in ../Resources
        std::filesystem::path bundlePath = appPath.toStdString();
        return bundlePath.parent_path() / "Resources" / RESOURCES_DIR;
    }
#endif
    return std::filesystem::path(appPath.toStdString()) / RESOURCES_DIR;
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
