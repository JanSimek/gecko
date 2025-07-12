#define QT_NO_EMIT
#include "Application.h"

#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>

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

    ResourceManager::getInstance().addDataPath(resourcePath);

    initUI();

    // Create AppData with SFML window from the Qt widget
    _appData = std::make_shared<AppData>(AppData{ 
        std::shared_ptr<sf::RenderWindow>(_mainWindow->getSFMLWidget()->getRenderWindow(), [](sf::RenderWindow*) {}), 
        _stateMachine,
        _mainWindow.get()
    });

    // TODO: show configuration window if no map is selected
    loadMap(mapPath);
    
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

Application::~Application() {
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
