#define QT_NO_EMIT
#include "Application.h"

#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>

#include "state/loader/MapLoader.h"
#include "util/ResourceManager.h"
#include "util/QtDialogs.h"
#include "ui/MainWindow.h"
#include "ui/EditorWidget.h"
#include "ui/LoadingWidget.h"

namespace geck {

Application::Application(int argc, char** argv, const std::filesystem::path& resourcePath, const std::filesystem::path& mapPath)
    : _qtApp(std::make_unique<QApplication>(argc, argv))
    , _mainWindow(nullptr) {

    // Set application metadata
    _qtApp->setApplicationName("GECK::Mapper");
    _qtApp->setApplicationDisplayName("Fallout 2 Map Editor");
    _qtApp->setApplicationVersion("0.1");
    
    // Process command line arguments
    std::string finalMapPath = processCommandLineArgs(argc, argv, resourcePath, mapPath);

    initUI();

    // Load the map (either from command line or show dialog)
    loadMap(finalMapPath);
}

void Application::loadMap(const std::filesystem::path& mapPath) {
    if (mapPath.empty()) {
        // No map specified, show file dialog to select one
        auto selectedMapPath = geck::QtDialogs::openFile("Choose Fallout 2 map to load", "",
            { {"Fallout 2 map (.map)", "*.map"} });
        
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

std::string Application::processCommandLineArgs(int /*argc*/, char** /*argv*/, const std::filesystem::path& resourcePath, const std::filesystem::path& mapPath) {
    // Set up Qt6 command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("Fallout 2 map editor");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Define command line options
    QCommandLineOption dataOption(QStringList() << "d" << "data",
        "Path to the Fallout 2 directory or individual data files, e.g. master.dat and critter.dat",
        "path", QString::fromStdString(resourcePath.string()));
    parser.addOption(dataOption);
    
    QCommandLineOption mapOption(QStringList() << "m" << "map",
        "Path to the map file to load",
        "mapfile");
    parser.addOption(mapOption);
    
    QCommandLineOption debugOption("debug", "Show debug messages");
    parser.addOption(debugOption);

    // Process command line arguments
    parser.process(*_qtApp);
    
    // Handle options
    if (parser.isSet(debugOption)) {
        spdlog::set_pattern("[%^%l%$] [thread %t] %v");
        spdlog::set_level(spdlog::level::debug);
    }
    
    if (parser.isSet(dataOption)) {
        QString dataPath = parser.value(dataOption);
        ResourceManager::getInstance().addDataPath(dataPath.toStdString());
    } else {
        auto dir = QtDialogs::selectFolder("Select Fallout 2 \"data\" directory which contains maps", resourcePath.string());
        if (!dir.empty()) {
            spdlog::info("User selected data directory: {}", dir);
            ResourceManager::getInstance().addDataPath(dir);
        }
    }

    // Return the map path from command line or the passed mapPath
    return parser.isSet(mapOption) ? parser.value(mapOption).toStdString() : mapPath.string();
}

Application::~Application() {
    // Clean up in proper order, but let Qt handle the final cleanup
    if (_mainWindow) {
        _mainWindow->stopGameLoop();
    }
}

void Application::initUI() {
    // Create Qt6 main window
    _mainWindow = std::make_unique<MainWindow>();
    _mainWindow->show();
    spdlog::info("Qt6 main window created and shown");
}

void Application::run() {
    // Start the Qt6 game loop in the main window
    _mainWindow->startGameLoop();
    
    // Execute Qt application event loop
    int result = _qtApp->exec();
    spdlog::info("Qt application exited with code: {}", result);
}

bool Application::isRunning() const {
    return _mainWindow && _mainWindow->isVisible();
}

} // namespace geck
