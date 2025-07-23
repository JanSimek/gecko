#define QT_NO_EMIT
#include "Application.h"

#include <spdlog/spdlog.h>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>
#include <QIcon>
#include <QCoreApplication>

#include "state/loader/MapLoader.h"
#include "util/ResourceManager.h"
#include "util/QtDialogs.h"
#include "ui/MainWindow.h"
#include "ui/EditorWidget.h"
#include "ui/LoadingWidget.h"

namespace geck {

Application::Application(int argc, char** argv)
    : _qtApp(std::make_unique<QApplication>(argc, argv))
    , _mainWindow(nullptr) {

    _qtApp->setApplicationName("Gecko");
    _qtApp->setApplicationDisplayName("Gecko");
    _qtApp->setApplicationVersion("0.1");
    
    // Set application icon with platform-aware path
    std::filesystem::path iconPath;
    
#ifdef __APPLE__
    // Check if we're running from a macOS app bundle
    QString appPath = QCoreApplication::applicationDirPath();
    if (appPath.contains(".app/Contents/MacOS")) {
        // We're in a bundle, icon is in ../Resources
        iconPath = appPath.toStdString();
        iconPath = iconPath.parent_path() / "Resources" / "icon.png";
    } else {
        // Not in a bundle, use relative path
        iconPath = "resources/icon.png";
    }
#else
    // For Windows and Linux, use relative path
    iconPath = "resources/icon.png";
#endif
    
    QIcon appIcon(QString::fromStdString(iconPath.string()));
    _qtApp->setWindowIcon(appIcon);

    const std::string finalMapPath = processCommandLineArgs();

    initUI();

    loadMap(finalMapPath);
}

void Application::loadMap(const std::filesystem::path& mapPath) {
    if (mapPath.empty()) {
        // No map specified, show file dialog to select one
        QString selectedMapQString = QtDialogs::openMapFile(nullptr, "Choose Fallout 2 map to load");
        std::filesystem::path selectedMapPath = selectedMapQString.toStdString();

        if (selectedMapPath.empty()) {
            spdlog::info("No map file selected, starting empty editor");
            // For now, just show empty main window
            // TODO: Could show a "New Map" wizard or welcome screen
            return;
        }

        // Recursively call with the selected path
        loadMap(selectedMapPath);
        return;
    }

    // Create loading widget and show it in main window
    auto loadingWidget = std::make_unique<LoadingWidget>();

    // Add map loader
    loadingWidget->addLoader(std::make_unique<MapLoader>(mapPath, -1, [this](auto map) {
        // When loading is complete, create editor widget and switch to it
        auto editorWidget = std::make_unique<EditorWidget>(std::move(map));
        _mainWindow->setEditorWidget(std::move(editorWidget));
    }));

    // Connect loading complete signal
    QObject::connect(loadingWidget.get(), &LoadingWidget::loadingComplete, loadingWidget.get(), []() {
        // Loading widget will automatically be replaced by editor widget
        // when the map loader completes
        spdlog::info("Map loading completed");
    });

    // Show loading widget in main window
    _mainWindow->setLoadingWidget(std::move(loadingWidget));
}

std::string Application::processCommandLineArgs() {
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.setApplicationDescription("Gecko - Fallout 2 map editor");

    // Determine the default resources path based on platform
    std::filesystem::path default_resources_path;
    
#ifdef __APPLE__
    // Check if we're running from a macOS app bundle
    QString appPath = QCoreApplication::applicationDirPath();
    if (appPath.contains(".app/Contents/MacOS")) {
        // We're in a bundle, resources are in ../Resources
        default_resources_path = appPath.toStdString();
        default_resources_path = default_resources_path.parent_path() / "Resources" / geck::Application::RESOURCES_DIR;
    } else {
        // Not in a bundle, use current directory
        default_resources_path = std::filesystem::current_path() / geck::Application::RESOURCES_DIR;
    }
#else
    // For Windows and Linux, use current directory
    default_resources_path = std::filesystem::current_path() / geck::Application::RESOURCES_DIR;
#endif

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

    QString dataPath = parser.value(dataOption);
    spdlog::info("Added {} as the default path for loading game files", dataPath.toStdString());
    ResourceManager::getInstance().addDataPath(dataPath.toStdString());

    // TODO: this dialog will be available in the configuration screen to append path to data paths
    /*
    auto dir = QtDialogs::selectFolder("Select Fallout 2 \"data\" directory which contains maps", resources_path.string());
    if (!dir.empty()) {
        spdlog::info("User selected data directory: {}", dir);
        ResourceManager::getInstance().addDataPath(dir);
    }
    */

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
    _mainWindow->show();
}

void Application::run() {
    _mainWindow->startGameLoop();

    int result = _qtApp->exec();
    spdlog::debug("Application exited with code: {}", result);
}

bool Application::isRunning() const {
    return _mainWindow && _mainWindow->isVisible();
}


} // namespace geck
