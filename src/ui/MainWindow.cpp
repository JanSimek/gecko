#define QT_NO_EMIT
#include "MainWindow.h"
#include "SFMLWidget.h"
#include "../state/StateMachine.h"

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
    // TODO: Connect to new map signal
    
    QAction* openAction = _fileMenu->addAction("&Open Map");
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip("Open an existing map");
    // TODO: Connect to open map signal
    
    QAction* saveAction = _fileMenu->addAction("&Save Map");
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setStatusTip("Save current map");
    // TODO: Connect to save map signal
    
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
    // TODO: Connect to show objects signal
    
    QAction* showCrittersAction = _viewMenu->addAction("Show &Critters");
    showCrittersAction->setCheckable(true);
    showCrittersAction->setChecked(true);
    // TODO: Connect to show critters signal
    
    QAction* showWallsAction = _viewMenu->addAction("Show &Walls");
    showWallsAction->setCheckable(true);
    showWallsAction->setChecked(true);
    // TODO: Connect to show walls signal
    
    QAction* showRoofsAction = _viewMenu->addAction("Show &Roofs");
    showRoofsAction->setCheckable(true);
    showRoofsAction->setChecked(true);
    // TODO: Connect to show roofs signal
    
    QAction* showScrollBlkAction = _viewMenu->addAction("Show Scroll &Blockers");
    showScrollBlkAction->setCheckable(true);
    showScrollBlkAction->setChecked(false);
    // TODO: Connect to show scroll blockers signal
    
    _viewMenu->addSeparator();
    
    // Elevation submenu
    _elevationMenu = _viewMenu->addMenu("&Elevation");
    
    QAction* elevation0Action = _elevationMenu->addAction("Elevation &0");
    elevation0Action->setCheckable(true);
    elevation0Action->setChecked(true);
    // TODO: Connect to elevation change signal
    
    QAction* elevation1Action = _elevationMenu->addAction("Elevation &1");
    elevation1Action->setCheckable(true);
    // TODO: Connect to elevation change signal
    
    QAction* elevation2Action = _elevationMenu->addAction("Elevation &2");
    elevation2Action->setCheckable(true);
    // TODO: Connect to elevation change signal
}

void MainWindow::setupToolBar() {
    _mainToolBar = addToolBar("Main");
    
    // Selection mode action
    QAction* selectionModeAction = _mainToolBar->addAction("Selection Mode");
    selectionModeAction->setStatusTip("Toggle selection mode (Objects/Floor/Roof/All)");
    // TODO: Connect to selection mode signal
    
    _mainToolBar->addSeparator();
    
    // Rotate action
    QAction* rotateAction = _mainToolBar->addAction("Rotate");
    rotateAction->setShortcut(QKeySequence("R"));
    rotateAction->setStatusTip("Rotate selected object");
    // TODO: Connect to rotate signal
}

void MainWindow::setupDockWidgets() {
    // Map Info dock
    _mapInfoDock = new QDockWidget("Map Information", this);
    _mapInfoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    QWidget* mapInfoWidget = new QWidget();
    QVBoxLayout* mapInfoLayout = new QVBoxLayout(mapInfoWidget);
    mapInfoLayout->addWidget(new QLabel("Map info will be displayed here"));
    _mapInfoDock->setWidget(mapInfoWidget);
    
    addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
    
    // Selected Object dock
    _selectedObjectDock = new QDockWidget("Selected Object", this);
    _selectedObjectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    QWidget* selectedObjectWidget = new QWidget();
    QVBoxLayout* selectedObjectLayout = new QVBoxLayout(selectedObjectWidget);
    selectedObjectLayout->addWidget(new QLabel("Selected object properties will be displayed here"));
    _selectedObjectDock->setWidget(selectedObjectWidget);
    
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

} // namespace geck