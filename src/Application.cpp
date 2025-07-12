#define QT_NO_EMIT
#include "Application.h"

#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include "state/EditorState.h"
#include "state/LoadingState.h"
#include "state/ConfigurationState.h"
#include "state/StateMachine.h"
#include "state/loader/MapLoader.h"
#include "util/ResourceManager.h"
#include "util/QtDialogs.h"
#include "ui/MainWindow.h"
#include "ui/SFMLWidget.h"

namespace geck {

Application::Application(int argc, char** argv, const std::filesystem::path& resourcePath, const std::filesystem::path& mapPath)
    : _qtApp(std::make_unique<QApplication>(argc, argv))
    , _mainWindow(nullptr)
    , _stateMachine(std::make_shared<StateMachine>()) {

    // Set application metadata
    _qtApp->setApplicationName("GECK::Mapper");
    _qtApp->setApplicationDisplayName("Fallout 2 Map Editor");
    _qtApp->setApplicationVersion("0.1");
    
    // Process command line arguments
    std::string finalMapPath = processCommandLineArgs(argc, argv, resourcePath, mapPath);

    initUI();

    // Create AppData with SFML window from the Qt widget
    _appData = std::make_shared<AppData>(AppData{ 
        _mainWindow->getSFMLWidget()->getRenderWindow(),
        _stateMachine,
        _mainWindow.get()
    });

    // Load the map (either from command line or show dialog)
    loadMap(finalMapPath);
    
    // Set the state machine in the main window after loading initial state
    _mainWindow->setStateMachine(_stateMachine);
}

void Application::loadMap(const std::filesystem::path& mapPath) {
    if (mapPath.empty()) {
        // No map specified, show file dialog to select one
        auto selectedMapPath = geck::QtDialogs::openFile("Choose Fallout 2 map to load", "",
            { {"Fallout 2 map (.map)", "*.map"} });
        
        if (selectedMapPath.empty()) {
            spdlog::info("No map file selected, starting with configuration screen");
            _stateMachine->push(std::make_unique<ConfigurationState>());
            return;
        }
        
        // Recursively call with the selected path
        loadMap(selectedMapPath);
        return;
    }

    auto loading_state = std::make_unique<LoadingState>(_appData);
    loading_state->addLoader(std::make_unique<MapLoader>(mapPath, -1, [this](auto map) {
        _appData->stateMachine->push(std::make_unique<EditorState>(_appData, std::move(map)), true);
    }));

    _stateMachine->push(std::move(loading_state));
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
    
    // Clear the state machine before Qt widget destruction
    if (_stateMachine) {
        while (!_stateMachine->empty()) {
            _stateMachine->pop();
        }
        _stateMachine.reset();
    }
    
    // Clear app data reference
    if (_appData) {
        _appData.reset();
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
