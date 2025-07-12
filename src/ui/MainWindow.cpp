#define QT_NO_EMIT
#include "MainWindow.h"
#include "SFMLWidget.h"
#include "Qt6SelectedObjectPanel.h"
#include "Qt6MapInfoPanel.h"
#include "../state/StateMachine.h"
#include "../state/EditorState.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QLabel>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QAction>
#include <QActionGroup>
#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>

namespace geck {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , _sfmlWidget(nullptr)
    , _gameLoopTimer(new QTimer(this))
    , _menuBar(nullptr)
    , _fileMenu(nullptr)
    , _viewMenu(nullptr)
    , _elevationMenu(nullptr)
    , _mainToolBar(nullptr)
    , _mapInfoDock(nullptr)
    , _selectedObjectDock(nullptr)
    , _tileSelectionDock(nullptr)
    , _selectedObjectPanel(nullptr)
    , _mapInfoPanel(nullptr)
    , _isRunning(false) {
    
    setWindowTitle("GECK::Mapper - Fallout 2 Map Editor");
    setMinimumSize(1024, 768);
    
    setupUI();
    
    // Connect timer to update loop
    connect(_gameLoopTimer, &QTimer::timeout, this, &MainWindow::updateAndRender);
}

MainWindow::~MainWindow() {
    stopGameLoop();
}

void MainWindow::setStateMachine(std::shared_ptr<StateMachine> stateMachine) {
    _stateMachine = stateMachine;
    if (_sfmlWidget) {
        _sfmlWidget->setStateMachine(stateMachine);
    }
}

void MainWindow::setupUI() {
    // Create central SFML widget
    _sfmlWidget = new SFMLWidget(this);
    setCentralWidget(_sfmlWidget);
    
    setupMenuBar();
    setupToolBar();
    setupDockWidgets();
}

void MainWindow::setupMenuBar() {
    _menuBar = menuBar();
    
    // File Menu
    _fileMenu = _menuBar->addMenu("&File");
    
    QAction* newAction = _fileMenu->addAction("&New Map");
    newAction->setShortcut(QKeySequence::New);
    newAction->setStatusTip("Create a new map");
    connect(newAction, &QAction::triggered, this, &MainWindow::newMapRequested);
    
    QAction* openAction = _fileMenu->addAction("&Open Map");
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip("Open an existing map");
    connect(openAction, &QAction::triggered, this, &MainWindow::openMapRequested);
    
    QAction* saveAction = _fileMenu->addAction("&Save Map");
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setStatusTip("Save current map");
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveMapRequested);
    
    _fileMenu->addSeparator();
    
    QAction* quitAction = _fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setStatusTip("Exit the application");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    
    // View Menu
    _viewMenu = _menuBar->addMenu("&View");
    
    QAction* showObjectsAction = _viewMenu->addAction("Show &Objects");
    showObjectsAction->setCheckable(true);
    showObjectsAction->setChecked(true);
    connect(showObjectsAction, &QAction::toggled, this, &MainWindow::showObjectsToggled);
    
    QAction* showCrittersAction = _viewMenu->addAction("Show &Critters");
    showCrittersAction->setCheckable(true);
    showCrittersAction->setChecked(true);
    connect(showCrittersAction, &QAction::toggled, this, &MainWindow::showCrittersToggled);
    
    QAction* showWallsAction = _viewMenu->addAction("Show &Walls");
    showWallsAction->setCheckable(true);
    showWallsAction->setChecked(true);
    connect(showWallsAction, &QAction::toggled, this, &MainWindow::showWallsToggled);
    
    QAction* showRoofsAction = _viewMenu->addAction("Show &Roofs");
    showRoofsAction->setCheckable(true);
    showRoofsAction->setChecked(true);
    connect(showRoofsAction, &QAction::toggled, this, &MainWindow::showRoofsToggled);
    
    QAction* showScrollBlkAction = _viewMenu->addAction("Show Scroll &Blockers");
    showScrollBlkAction->setCheckable(true);
    showScrollBlkAction->setChecked(false);
    connect(showScrollBlkAction, &QAction::toggled, this, &MainWindow::showScrollBlockersToggled);
    
    _viewMenu->addSeparator();
    
    // Elevation submenu
    _elevationMenu = _viewMenu->addMenu("&Elevation");
    
    // Create action group for mutually exclusive elevation selection
    QActionGroup* elevationGroup = new QActionGroup(this);
    
    QAction* elevation0Action = _elevationMenu->addAction("Elevation &0");
    elevation0Action->setCheckable(true);
    elevation0Action->setChecked(true);
    elevation0Action->setData(0);
    elevationGroup->addAction(elevation0Action);
    connect(elevation0Action, &QAction::triggered, [this]() { elevationChanged(0); });
    
    QAction* elevation1Action = _elevationMenu->addAction("Elevation &1");
    elevation1Action->setCheckable(true);
    elevation1Action->setData(1);
    elevationGroup->addAction(elevation1Action);
    connect(elevation1Action, &QAction::triggered, [this]() { elevationChanged(1); });
    
    QAction* elevation2Action = _elevationMenu->addAction("Elevation &2");
    elevation2Action->setCheckable(true);
    elevation2Action->setData(2);
    elevationGroup->addAction(elevation2Action);
    connect(elevation2Action, &QAction::triggered, [this]() { elevationChanged(2); });
}

void MainWindow::setupToolBar() {
    _mainToolBar = addToolBar("Main");
    
    // Selection mode action - start with "All" mode
    QAction* selectionModeAction = _mainToolBar->addAction("Mode: All");
    selectionModeAction->setStatusTip("Toggle selection mode (Objects/Floor/Roof/All)");
    connect(selectionModeAction, &QAction::triggered, this, &MainWindow::selectionModeRequested);
    
    // Update selection mode button text when mode changes
    connect(this, &MainWindow::selectionModeRequested, [selectionModeAction]() {
        static int currentMode = 0; // 0=All, 1=Objects, 2=Floor, 3=Roof
        currentMode = (currentMode + 1) % 4;
        
        switch(currentMode) {
            case 0: selectionModeAction->setText("Mode: All"); break;
            case 1: selectionModeAction->setText("Mode: Objects"); break;
            case 2: selectionModeAction->setText("Mode: Floor"); break;
            case 3: selectionModeAction->setText("Mode: Roof"); break;
        }
    });
    
    _mainToolBar->addSeparator();
    
    // Rotate action
    QAction* rotateAction = _mainToolBar->addAction("Rotate");
    rotateAction->setShortcut(QKeySequence("R"));
    rotateAction->setStatusTip("Rotate selected object");
    connect(rotateAction, &QAction::triggered, this, &MainWindow::rotateObjectRequested);
}

void MainWindow::setupDockWidgets() {
    // Map Info dock
    _mapInfoDock = new QDockWidget("Map Information", this);
    _mapInfoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    // Create and set the Qt6 MapInfoPanel
    _mapInfoPanel = new Qt6MapInfoPanel();
    _mapInfoDock->setWidget(_mapInfoPanel);
    
    addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
    
    // Selected Object dock
    _selectedObjectDock = new QDockWidget("Selected Object", this);
    _selectedObjectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    // Create and set the Qt6 SelectedObjectPanel
    _selectedObjectPanel = new Qt6SelectedObjectPanel();
    _selectedObjectDock->setWidget(_selectedObjectPanel);
    
    addDockWidget(Qt::RightDockWidgetArea, _selectedObjectDock);
    
    // Tile Selection dock
    _tileSelectionDock = new QDockWidget("Tile Selection", this);
    _tileSelectionDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    QWidget* tileSelectionWidget = new QWidget();
    QVBoxLayout* tileSelectionLayout = new QVBoxLayout(tileSelectionWidget);
    tileSelectionLayout->addWidget(new QLabel("Tile selection grid will be displayed here"));
    _tileSelectionDock->setWidget(tileSelectionWidget);
    
    addDockWidget(Qt::LeftDockWidgetArea, _tileSelectionDock);
}

void MainWindow::startGameLoop() {
    if (!_isRunning) {
        _isRunning = true;
        _gameLoopTimer->start(16); // ~60 FPS
        spdlog::info("Qt6 game loop started");
    }
}

void MainWindow::stopGameLoop() {
    if (_isRunning) {
        _isRunning = false;
        _gameLoopTimer->stop();
        spdlog::info("Qt6 game loop stopped");
    }
}

void MainWindow::updateAndRender() {
    if (_sfmlWidget && _isRunning) {
        _sfmlWidget->updateAndRender();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    stopGameLoop();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (_sfmlWidget) {
        sf::Event sfmlEvent;
        convertQtEventToSFML(event, sfmlEvent, true);
        _sfmlWidget->handleSFMLEvent(sfmlEvent);
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    if (_sfmlWidget) {
        sf::Event sfmlEvent;
        convertQtEventToSFML(event, sfmlEvent, false);
        _sfmlWidget->handleSFMLEvent(sfmlEvent);
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::convertQtEventToSFML(QKeyEvent* qtEvent, sf::Event& sfmlEvent, bool pressed) {
    sfmlEvent.type = pressed ? sf::Event::KeyPressed : sf::Event::KeyReleased;
    
    // Convert Qt key to SFML key
    switch (qtEvent->key()) {
        case Qt::Key_Escape: sfmlEvent.key.code = sf::Keyboard::Escape; break;
        case Qt::Key_Left: sfmlEvent.key.code = sf::Keyboard::Left; break;
        case Qt::Key_Right: sfmlEvent.key.code = sf::Keyboard::Right; break;
        case Qt::Key_Up: sfmlEvent.key.code = sf::Keyboard::Up; break;
        case Qt::Key_Down: sfmlEvent.key.code = sf::Keyboard::Down; break;
        case Qt::Key_N: sfmlEvent.key.code = sf::Keyboard::N; break;
        case Qt::Key_O: sfmlEvent.key.code = sf::Keyboard::O; break;
        case Qt::Key_S: sfmlEvent.key.code = sf::Keyboard::S; break;
        case Qt::Key_Q: sfmlEvent.key.code = sf::Keyboard::Q; break;
        case Qt::Key_R: sfmlEvent.key.code = sf::Keyboard::R; break;
        case Qt::Key_M: sfmlEvent.key.code = sf::Keyboard::M; break;
        default: sfmlEvent.key.code = sf::Keyboard::Unknown; break;
    }
    
    // Convert modifiers
    sfmlEvent.key.control = qtEvent->modifiers() & Qt::ControlModifier;
    sfmlEvent.key.shift = qtEvent->modifiers() & Qt::ShiftModifier;
    sfmlEvent.key.alt = qtEvent->modifiers() & Qt::AltModifier;
    sfmlEvent.key.system = qtEvent->modifiers() & Qt::MetaModifier;
}

void MainWindow::connectToEditorState() {
    if (!_stateMachine || _stateMachine->empty()) {
        return;
    }
    
    // Try to cast current state to EditorState
    EditorState* editorState = dynamic_cast<EditorState*>(&_stateMachine->top());
    if (!editorState) {
        return; // Current state is not an EditorState
    }
    
    // Connect MainWindow signals to EditorState methods via lambdas
    connect(this, &MainWindow::newMapRequested, [editorState]() {
        editorState->createNewMap();
    });
    connect(this, &MainWindow::openMapRequested, [editorState]() {
        editorState->openMap();
    });
    connect(this, &MainWindow::saveMapRequested, [editorState]() {
        editorState->saveMap();
    });
    
    // Connect view toggles to EditorState visibility flags
    connect(this, &MainWindow::showObjectsToggled, [editorState](bool enabled) {
        editorState->setShowObjects(enabled);
    });
    connect(this, &MainWindow::showCrittersToggled, [editorState](bool enabled) {
        editorState->setShowCritters(enabled);
    });
    connect(this, &MainWindow::showWallsToggled, [editorState](bool enabled) {
        editorState->setShowWalls(enabled);
    });
    connect(this, &MainWindow::showRoofsToggled, [editorState](bool enabled) {
        editorState->setShowRoof(enabled);
    });
    connect(this, &MainWindow::showScrollBlockersToggled, [editorState](bool enabled) {
        editorState->setShowScrollBlk(enabled);
    });
    
    // Connect elevation changes
    connect(this, &MainWindow::elevationChanged, [editorState](int elevation) {
        editorState->changeElevation(elevation);
    });
    
    // Connect toolbar actions  
    connect(this, &MainWindow::selectionModeRequested, [editorState]() {
        editorState->cycleSelectionMode();
    });
    connect(this, &MainWindow::rotateObjectRequested, [editorState]() {
        editorState->rotateSelectedObject();
    });
    
    // Connect EditorState's objectSelected signal to the Qt6SelectedObjectPanel
    if (_selectedObjectPanel) {
        connect(editorState, &EditorState::objectSelected, _selectedObjectPanel, &Qt6SelectedObjectPanel::selectObject);
        spdlog::info("Connected EditorState objectSelected signal to Qt6SelectedObjectPanel");
    }
    
    // Update map info panel with current map
    if (_mapInfoPanel) {
        // For now, we need to access the EditorState's map directly
        // TODO: Add a signal for map changes in EditorState
        updateMapInfo(editorState->getMap());
    }
    
    spdlog::info("Qt6 menu connected to EditorState");
}

void MainWindow::updateMapInfo(Map* map) {
    if (_mapInfoPanel) {
        _mapInfoPanel->setMap(map);
    }
}

} // namespace geck