#define QT_NO_EMIT
#include "MainWindow.h"
#include "EditorWidget.h"
#include "LoadingWidget.h"
#include "SFMLWidget.h"
#include "SelectionPanel.h"
#include "MapInfoPanel.h"
#include "../state/loader/MapLoader.h"
#include "../util/Types.h"

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
    , _centralStack(nullptr)
    , _gameLoopTimer(new QTimer(this))
    , _currentEditorWidget(nullptr)
    , _currentLoadingWidget(nullptr)
    , _menuBar(nullptr)
    , _fileMenu(nullptr)
    , _viewMenu(nullptr)
    , _elevationMenu(nullptr)
    , _mainToolBar(nullptr)
    , _mapInfoDock(nullptr)
    , _selectionDock(nullptr)
    , _selectionPanel(nullptr)
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

void MainWindow::setEditorWidget(std::unique_ptr<EditorWidget> editorWidget) {
    _currentEditorWidget = editorWidget.release();
    _centralStack->addWidget(_currentEditorWidget);
    _centralStack->setCurrentWidget(_currentEditorWidget);
    
    // Initialize the editor widget
    _currentEditorWidget->init();
    
    // Connect signals
    connectToEditorWidget();
    
    // Update map info panel
    if (_currentEditorWidget->getMap()) {
        updateMapInfo(_currentEditorWidget->getMap());
    }
}

void MainWindow::setLoadingWidget(std::unique_ptr<LoadingWidget> loadingWidget) {
    _currentLoadingWidget = loadingWidget.release();
    _centralStack->addWidget(_currentLoadingWidget);
    _centralStack->setCurrentWidget(_currentLoadingWidget);
    
    // Start the loading process
    _currentLoadingWidget->start();
}

void MainWindow::setupUI() {
    // Create central stacked widget to hold loading and editor widgets
    _centralStack = new QStackedWidget(this);
    setCentralWidget(_centralStack);
    
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
    
    // Dock widgets submenu
    QAction* showSelectionAction = _viewMenu->addAction("Show &Selection Panel");
    showSelectionAction->setCheckable(true);
    showSelectionAction->setChecked(true);
    connect(showSelectionAction, &QAction::toggled, [this](bool visible) {
        if (visible) {
            _selectionDock->show();
        } else {
            _selectionDock->hide();
        }
    });
    
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
        static SelectionMode currentMode = SelectionMode::ALL;
        currentMode = static_cast<SelectionMode>((static_cast<int>(currentMode) + 1) % static_cast<int>(SelectionMode::NUM_SELECTION_TYPES));
        
        QString modeText = QString("Mode: %1").arg(selectionModeToString(currentMode));
        selectionModeAction->setText(modeText);
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
    
    // Create and set the MapInfoPanel
    _mapInfoPanel = new MapInfoPanel();
    _mapInfoDock->setWidget(_mapInfoPanel);
    
    addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
    
    // Selection dock (unified object and tile selection)
    _selectionDock = new QDockWidget("Selection", this);
    _selectionDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    // Create and set the unified SelectionPanel
    _selectionPanel = new SelectionPanel();
    _selectionDock->setWidget(_selectionPanel);
    
    addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
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
    if (_currentEditorWidget && _isRunning) {
        // Update and render current editor widget
        SFMLWidget* sfmlWidget = _currentEditorWidget->getSFMLWidget();
        if (sfmlWidget) {
            sfmlWidget->updateAndRender();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    stopGameLoop();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (_currentEditorWidget) {
        SFMLWidget* sfmlWidget = _currentEditorWidget->getSFMLWidget();
        if (sfmlWidget) {
            sf::Event sfmlEvent;
            convertQtEventToSFML(event, sfmlEvent, true);
            sfmlWidget->handleSFMLEvent(sfmlEvent);
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    if (_currentEditorWidget) {
        SFMLWidget* sfmlWidget = _currentEditorWidget->getSFMLWidget();
        if (sfmlWidget) {
            sf::Event sfmlEvent;
            convertQtEventToSFML(event, sfmlEvent, false);
            sfmlWidget->handleSFMLEvent(sfmlEvent);
        }
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

void MainWindow::connectToEditorWidget() {
    if (!_currentEditorWidget) {
        return;
    }
    
    // Connect MainWindow signals to EditorWidget methods via lambdas
    connect(this, &MainWindow::newMapRequested, [this]() {
        _currentEditorWidget->createNewMap();
    });
    connect(this, &MainWindow::openMapRequested, [this]() {
        _currentEditorWidget->openMap();
    });
    connect(this, &MainWindow::saveMapRequested, [this]() {
        _currentEditorWidget->saveMap();
    });
    
    // Connect view toggles to EditorWidget visibility flags
    connect(this, &MainWindow::showObjectsToggled, [this](bool enabled) {
        _currentEditorWidget->setShowObjects(enabled);
    });
    connect(this, &MainWindow::showCrittersToggled, [this](bool enabled) {
        _currentEditorWidget->setShowCritters(enabled);
    });
    connect(this, &MainWindow::showWallsToggled, [this](bool enabled) {
        _currentEditorWidget->setShowWalls(enabled);
    });
    connect(this, &MainWindow::showRoofsToggled, [this](bool enabled) {
        _currentEditorWidget->setShowRoof(enabled);
    });
    connect(this, &MainWindow::showScrollBlockersToggled, [this](bool enabled) {
        _currentEditorWidget->setShowScrollBlk(enabled);
    });
    
    // Connect elevation changes
    connect(this, &MainWindow::elevationChanged, [this](int elevation) {
        _currentEditorWidget->changeElevation(elevation);
    });
    
    // Connect toolbar actions  
    connect(this, &MainWindow::selectionModeRequested, [this]() {
        _currentEditorWidget->cycleSelectionMode();
    });
    connect(this, &MainWindow::rotateObjectRequested, [this]() {
        _currentEditorWidget->rotateSelectedObject();
    });
    
    // Connect EditorWidget's selection signals to the unified SelectionPanel
    if (_selectionPanel) {
        // Set the map reference for the selection panel
        _selectionPanel->setMap(_currentEditorWidget->getMap());
        
        // Connect object selection signals
        connect(_currentEditorWidget, &EditorWidget::objectSelected, _selectionPanel, &SelectionPanel::selectObject);
        
        // Connect tile selection signals
        connect(_currentEditorWidget, &EditorWidget::tileSelected, _selectionPanel, &SelectionPanel::selectTile);
        connect(_currentEditorWidget, &EditorWidget::tileSelectionCleared, _selectionPanel, &SelectionPanel::clearSelection);
        
        spdlog::info("Connected EditorWidget selection signals to unified SelectionPanel");
    }
    
    // Connect map loading signal
    connect(_currentEditorWidget, &EditorWidget::mapLoadRequested, this, &MainWindow::handleMapLoadRequest);
    
    spdlog::info("Qt6 menu connected to EditorWidget");
}

void MainWindow::updateMapInfo(Map* map) {
    if (_mapInfoPanel) {
        _mapInfoPanel->setMap(map);
    }
}

void MainWindow::handleMapLoadRequest(const std::string& mapPath) {
    spdlog::info("MainWindow: Handling request to load map: {}", mapPath);
    
    // Create loading widget and show it
    auto loadingWidget = std::make_unique<LoadingWidget>();
    
    // Add map loader with callback to create new editor widget
    loadingWidget->addLoader(std::make_unique<MapLoader>(mapPath, -1, [this](auto map) {
        // When loading is complete, create new editor widget and switch to it
        auto editorWidget = std::make_unique<EditorWidget>(std::move(map));
        setEditorWidget(std::move(editorWidget));
    }));
    
    // Connect loading complete signal
    QObject::connect(loadingWidget.get(), &LoadingWidget::loadingComplete, loadingWidget.get(), []() {
        spdlog::info("Map loading completed from MainWindow");
    });
    
    // Show loading widget
    setLoadingWidget(std::move(loadingWidget));
}

} // namespace geck