#define QT_NO_EMIT
#include "MainWindow.h"
#include "EditorWidget.h"
#include "LoadingWidget.h"
#include "SFMLWidget.h"
#include "SelectionPanel.h"
#include "MapInfoPanel.h"
#include "TilePalettePanel.h"
#include "ObjectPalettePanel.h"
#include "FileBrowserPanel.h"
#include "../state/loader/MapLoader.h"
#include "../util/Types.h"
#include "../util/ResourceManager.h"
#include "../reader/lst/LstReader.h"
#include "../format/lst/Lst.h"

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
    , _editMenu(nullptr)
    , _viewMenu(nullptr)
    , _panelsMenu(nullptr)
    , _elevationMenu(nullptr)
    , _mainToolBar(nullptr)
    , _mapInfoDock(nullptr)
    , _selectionDock(nullptr)
    , _tilePaletteDock(nullptr)
    , _objectPaletteDock(nullptr)
    , _fileBrowserDock(nullptr)
    , _selectionPanel(nullptr)
    , _mapInfoPanel(nullptr)
    , _tilePalettePanel(nullptr)
    , _objectPalettePanel(nullptr)
    , _fileBrowserPanel(nullptr)
    , _mapInfoPanelAction(nullptr)
    , _selectionPanelAction(nullptr)
    , _tilePalettePanelAction(nullptr)
    , _objectPalettePanelAction(nullptr)
    , _fileBrowserPanelAction(nullptr)
    , _isRunning(false) {

    setWindowTitle("GECK::Mapper - Fallout 2 Map Editor");
    setMinimumSize(1024, 768);

    setupUI();

    // Restore dock widget state from previous session
    restoreDockWidgetState();

    // Update panel menu actions to reflect actual visibility after restoration
    QTimer::singleShot(200, this, &MainWindow::updatePanelMenuActions);

    // Connect timer to update loop
    connect(_gameLoopTimer, &QTimer::timeout, this, &MainWindow::updateAndRender);
}

MainWindow::~MainWindow() {
    // Save dock widget state before destruction
    saveDockWidgetState();
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
    setupPanelsMenu();
    setupStatusBar();
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

    // Edit Menu
    _editMenu = _menuBar->addMenu("&Edit");

    QAction* selectAllAction = _editMenu->addAction("Select &All");
    selectAllAction->setShortcut(QKeySequence("Ctrl+A"));
    selectAllAction->setStatusTip("Select all items of current type");
    connect(selectAllAction, &QAction::triggered, this, &MainWindow::selectAllRequested);

    QAction* deselectAllAction = _editMenu->addAction("&Deselect All");
    deselectAllAction->setShortcut(QKeySequence("Ctrl+D"));
    deselectAllAction->setStatusTip("Clear all selections");
    connect(deselectAllAction, &QAction::triggered, this, &MainWindow::deselectAllRequested);

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

    QAction* showHexGridAction = _viewMenu->addAction("Show &Hex Grid");
    showHexGridAction->setCheckable(true);
    showHexGridAction->setChecked(false);
    connect(showHexGridAction, &QAction::toggled, this, &MainWindow::showHexGridToggled);

    _viewMenu->addSeparator();

    // Panels submenu (will be set up later in setupPanelsMenu)
    _panelsMenu = _viewMenu->addMenu("&Panels");

    _viewMenu->addSeparator();

    // Dock Layout submenu
    QMenu* dockLayoutMenu = _viewMenu->addMenu("&Dock Layout");

    // Panel management actions (non-exclusive)
    QAction* resetLayoutAction = dockLayoutMenu->addAction("&Reset to Default Layout");
    resetLayoutAction->setStatusTip("Reset all panels to their default positions");
    connect(resetLayoutAction, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);

    QAction* redockAllAction = dockLayoutMenu->addAction("Re-&dock All Floating Panels");
    redockAllAction->setStatusTip("Dock all floating panels back to the main window");
    connect(redockAllAction, &QAction::triggered, [this]() {
        // Re-dock any floating panels
        if (_mapInfoDock->isFloating())
            _mapInfoDock->setFloating(false);
        if (_selectionDock->isFloating())
            _selectionDock->setFloating(false);
        if (_tilePaletteDock->isFloating())
            _tilePaletteDock->setFloating(false);
        if (_objectPaletteDock->isFloating())
            _objectPaletteDock->setFloating(false);
        if (_fileBrowserDock->isFloating())
            _fileBrowserDock->setFloating(false);
        spdlog::info("Re-docked all floating panels");
    });

    dockLayoutMenu->addSeparator();

    // Create action group for mutually exclusive dock layout selection
    QActionGroup* layoutGroup = new QActionGroup(this);

    QAction* verticalStackAction = dockLayoutMenu->addAction("&Vertical Stack (Right Side)");
    verticalStackAction->setCheckable(true);
    verticalStackAction->setChecked(true);
    verticalStackAction->setStatusTip("Stack Map Info and Selection panels vertically on the right");
    layoutGroup->addAction(verticalStackAction);
    connect(verticalStackAction, &QAction::triggered, [this]() {
        // Move panels to right side and split vertically
        addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
        addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
        splitDockWidget(_mapInfoDock, _selectionDock, Qt::Vertical);
    });

    QAction* horizontalStackAction = dockLayoutMenu->addAction("&Horizontal Stack (Right Side)");
    horizontalStackAction->setCheckable(true);
    horizontalStackAction->setStatusTip("Stack Map Info and Selection panels horizontally on the right");
    layoutGroup->addAction(horizontalStackAction);
    connect(horizontalStackAction, &QAction::triggered, [this]() {
        // Move panels to right side and split horizontally
        addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
        addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
        splitDockWidget(_mapInfoDock, _selectionDock, Qt::Horizontal);
    });

    QAction* tabbedLayoutAction = dockLayoutMenu->addAction("&Tabbed Layout (Right Side)");
    tabbedLayoutAction->setCheckable(true);
    tabbedLayoutAction->setStatusTip("Tab Map Info and Selection panels together on the right");
    layoutGroup->addAction(tabbedLayoutAction);
    connect(tabbedLayoutAction, &QAction::triggered, [this]() {
        // Move panels to right side and tabify them
        addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
        addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
        tabifyDockWidget(_mapInfoDock, _selectionDock);
        _mapInfoDock->raise();
    });

    QAction* bottomDockAction = dockLayoutMenu->addAction("&Bottom Dock");
    bottomDockAction->setCheckable(true);
    bottomDockAction->setStatusTip("Move Map Info and Selection panels to the bottom area");
    layoutGroup->addAction(bottomDockAction);
    connect(bottomDockAction, &QAction::triggered, [this]() {
        // Move both docks to bottom area
        addDockWidget(Qt::BottomDockWidgetArea, _mapInfoDock);
        addDockWidget(Qt::BottomDockWidgetArea, _selectionDock);
        splitDockWidget(_mapInfoDock, _selectionDock, Qt::Horizontal);
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
    _mapInfoDock->setObjectName("MapInfoDock"); // Unique name for state persistence
    _mapInfoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    _mapInfoDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);

    // Create and set the MapInfoPanel
    _mapInfoPanel = new MapInfoPanel();
    _mapInfoPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _mapInfoDock->setWidget(_mapInfoPanel);

    addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);

    // Selection dock (unified object and tile selection)
    _selectionDock = new QDockWidget("Selection", this);
    _selectionDock->setObjectName("SelectionDock"); // Unique name for state persistence
    _selectionDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    _selectionDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);

    // Create and set the unified SelectionPanel
    _selectionPanel = new SelectionPanel();
    _selectionPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _selectionDock->setWidget(_selectionPanel);

    addDockWidget(Qt::RightDockWidgetArea, _selectionDock);

    // Tile Palette dock
    _tilePaletteDock = new QDockWidget("Tile Palette", this);
    _tilePaletteDock->setObjectName("TilePaletteDock"); // Unique name for state persistence
    _tilePaletteDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    _tilePaletteDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);

    // Create and set the TilePalettePanel
    _tilePalettePanel = new TilePalettePanel();
    _tilePalettePanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    _tilePaletteDock->setWidget(_tilePalettePanel);

    addDockWidget(Qt::LeftDockWidgetArea, _tilePaletteDock);

    // Object Palette dock
    _objectPaletteDock = new QDockWidget("Object Palette", this);
    _objectPaletteDock->setObjectName("ObjectPaletteDock"); // Unique name for state persistence
    _objectPaletteDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    _objectPaletteDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);

    // Create and set the ObjectPalettePanel
    _objectPalettePanel = new ObjectPalettePanel();
    _objectPalettePanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    _objectPaletteDock->setWidget(_objectPalettePanel);

    addDockWidget(Qt::LeftDockWidgetArea, _objectPaletteDock);

    // File Browser dock
    _fileBrowserDock = new QDockWidget("File Browser", this);
    _fileBrowserDock->setObjectName("FileBrowserDock"); // Unique name for state persistence
    _fileBrowserDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    _fileBrowserDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);

    // Create and set the FileBrowserPanel
    _fileBrowserPanel = new FileBrowserPanel();
    _fileBrowserPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    _fileBrowserDock->setWidget(_fileBrowserPanel);

    addDockWidget(Qt::LeftDockWidgetArea, _fileBrowserDock);

    // Configure initial dock layout - vertical stacking instead of tabs
    splitDockWidget(_mapInfoDock, _selectionDock, Qt::Vertical);
    tabifyDockWidget(_tilePaletteDock, _objectPaletteDock);
    tabifyDockWidget(_objectPaletteDock, _fileBrowserDock);

    // Set minimum sizes for dock widgets to enable proper resizing
    _mapInfoDock->setMinimumSize(250, 200);
    _selectionDock->setMinimumSize(250, 200);
    _tilePaletteDock->setMinimumSize(300, 400);
    _objectPaletteDock->setMinimumSize(300, 400);
    _fileBrowserDock->setMinimumSize(250, 300);

    // Use resizeDocks to set initial dock widget sizes after layout is established
    QTimer::singleShot(0, this, [this]() {
        // Resize docks after the event loop processes the initial layout
        QList<QDockWidget*> rightDocks = { _mapInfoDock, _selectionDock };
        QList<int> rightSizes = { 300, 400 }; // Map info smaller, selection larger
        resizeDocks(rightDocks, rightSizes, Qt::Vertical);

        QList<QDockWidget*> leftDocks = { _tilePaletteDock }; // Only resize the container dock
        QList<int> leftSizes = { 350 };                       // Give adequate width for tile palette
        resizeDocks(leftDocks, leftSizes, Qt::Horizontal);

        spdlog::debug("Applied initial dock widget sizes");
    });

    // Set panels above the SFML widget to prevent redrawing issues
    // This is achieved by using QDockWidget's floating and layering features
    // Dock widgets are already rendered above the central widget by Qt's design
    spdlog::info("Dock widgets configured with proper z-order and panel features");
}

void MainWindow::setupStatusBar() {
    _statusBar = statusBar();

    // Create hex index label
    _hexIndexLabel = new QLabel("Hex: N/A");
    _hexIndexLabel->setMinimumWidth(80);
    _statusBar->addWidget(_hexIndexLabel);

    // Add a spacer to push hex info to the left
    _statusBar->addPermanentWidget(new QLabel(" "), 1);
}

void MainWindow::updateHexIndexDisplay(int hexIndex) {
    if (hexIndex >= 0) {
        _hexIndexLabel->setText(QString("Hex: %1").arg(hexIndex));
    } else {
        _hexIndexLabel->setText("Hex: N/A");
    }
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
            sf::Event sfmlEvent{ sf::Event::KeyPressed{ sf::Keyboard::Key::Unknown } };
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
            sf::Event sfmlEvent{ sf::Event::KeyReleased{ sf::Keyboard::Key::Unknown } };
            convertQtEventToSFML(event, sfmlEvent, false);
            sfmlWidget->handleSFMLEvent(sfmlEvent);
        }
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::convertQtEventToSFML(QKeyEvent* qtEvent, sf::Event& sfmlEvent, bool pressed) {
    // Convert Qt key to SFML key
    sf::Keyboard::Key key = sf::Keyboard::Key::Unknown;
    switch (qtEvent->key()) {
        case Qt::Key_Escape:
            key = sf::Keyboard::Key::Escape;
            break;
        case Qt::Key_Left:
            key = sf::Keyboard::Key::Left;
            break;
        case Qt::Key_Right:
            key = sf::Keyboard::Key::Right;
            break;
        case Qt::Key_Up:
            key = sf::Keyboard::Key::Up;
            break;
        case Qt::Key_Down:
            key = sf::Keyboard::Key::Down;
            break;
        case Qt::Key_N:
            key = sf::Keyboard::Key::N;
            break;
        case Qt::Key_O:
            key = sf::Keyboard::Key::O;
            break;
        case Qt::Key_S:
            key = sf::Keyboard::Key::S;
            break;
        case Qt::Key_Q:
            key = sf::Keyboard::Key::Q;
            break;
        case Qt::Key_R:
            key = sf::Keyboard::Key::R;
            break;
        case Qt::Key_M:
            key = sf::Keyboard::Key::M;
            break;
        default:
            key = sf::Keyboard::Key::Unknown;
            break;
    }

    // Create the proper SFML 3 event
    if (pressed) {
        sfmlEvent = sf::Event::KeyPressed{ key };
    } else {
        sfmlEvent = sf::Event::KeyReleased{ key };
    }

    // Note: SFML 3 events don't store modifier states directly
    // Modifiers should be checked using sf::Keyboard::isKeyPressed() when needed
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
    connect(this, &MainWindow::selectAllRequested, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->selectAll();
        }
    });
    connect(this, &MainWindow::deselectAllRequested, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->clearSelection();
        }
        if (_selectionPanel) {
            _selectionPanel->clearSelection();
        }
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

    connect(this, &MainWindow::showHexGridToggled, [this](bool enabled) {
        _currentEditorWidget->setShowHexGrid(enabled);
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

        // Connect efficient batched selection signal
        connect(_currentEditorWidget, &EditorWidget::selectionChanged, _selectionPanel, &SelectionPanel::handleSelectionChanged);

        // Keep legacy tile selection signals for single-item selections
        connect(_currentEditorWidget, &EditorWidget::tileSelected, _selectionPanel, &SelectionPanel::selectTile);
        connect(_currentEditorWidget, &EditorWidget::tileSelectionCleared, _selectionPanel, &SelectionPanel::clearSelection);

        spdlog::info("Connected EditorWidget selection signals to unified SelectionPanel");
    }

    // Connect TilePalettePanel to EditorWidget
    if (_tilePalettePanel) {
        // Set the map reference for the tile palette panel
        _tilePalettePanel->setMap(_currentEditorWidget->getMap());

        // Connect tile selection to enable tile placement mode
        connect(_tilePalettePanel, &TilePalettePanel::tileSelected,
            [this](int tileIndex) {
                if (tileIndex >= 0) {
                    // Default to floor for single placement and area fill (roof/floor detection is automatic for replace mode)
                    _currentEditorWidget->setTilePlacementMode(true, tileIndex, false);
                    spdlog::debug("Enabled tile placement mode with tile {}", tileIndex);
                } else {
                    // Disable tile placement mode
                    _currentEditorWidget->setTilePlacementMode(false);
                    spdlog::debug("Disabled tile placement mode");
                }
            });

        // Connect placement mode changes to update unified placement settings
        connect(_tilePalettePanel, &TilePalettePanel::placementModeChanged,
            [this](TilePalettePanel::PlacementMode mode) {
                // With unified placement mode, both single and area placement are supported
                // The EditorWidget will handle click vs drag detection
                _currentEditorWidget->setTilePlacementAreaFill(true);     // Enable drag support
                _currentEditorWidget->setTilePlacementReplaceMode(false); // Replace is automatic
                spdlog::debug("Set unified tile placement mode");
            });

        // Connect replace selected tiles signal for direct tile replacement
        connect(_tilePalettePanel, &TilePalettePanel::replaceSelectedTiles,
            [this](int newTileIndex) {
                // Tiles are replaced based on what's actually selected (floor/roof auto-detected)
                _currentEditorWidget->replaceSelectedTiles(newTileIndex);
            });

        // Connect interaction mode changes to enable/disable tile placement functionality
        connect(_tilePalettePanel, &TilePalettePanel::interactionModeChanged,
            [this](TilePalettePanel::InteractionMode mode) {
                bool tilePaintingEnabled = (mode == TilePalettePanel::InteractionMode::TILE_PAINTING);
                // TODO: Implement EditorWidget method to toggle tile painting interaction mode
                // _currentEditorWidget->setTilePaintingInteractionMode(tilePaintingEnabled);
                spdlog::debug("Interaction mode changed: tile painting {}", tilePaintingEnabled ? "enabled" : "disabled");
            });

        spdlog::info("Connected TilePalettePanel to EditorWidget");
    }

    // Connect hex hover signal to status bar
    connect(_currentEditorWidget, &EditorWidget::hexHoverChanged, this, &MainWindow::updateHexIndexDisplay);

    // Connect map loading signal
    connect(_currentEditorWidget, &EditorWidget::mapLoadRequested, this, &MainWindow::handleMapLoadRequest);

    spdlog::info("Qt6 menu connected to EditorWidget");
}

void MainWindow::updateMapInfo(Map* map) {
    if (_mapInfoPanel) {
        _mapInfoPanel->setMap(map);
    }

    // Load tiles into palette when map is loaded
    if (_tilePalettePanel && map) {
        // Load tile list from ResourceManager using the same method as original implementation
        try {
            auto& resourceManager = ResourceManager::getInstance();

            // Get the tiles.lst resource (it should already be loaded by MapLoader)
            const auto* tileList = resourceManager.getResource<Lst, std::string>("art/tiles/tiles.lst");

            if (tileList) {
                _tilePalettePanel->loadTiles(tileList);
                spdlog::info("Loaded tiles.lst into palette: {} tiles", tileList->list().size());
            } else {
                spdlog::error("Failed to get tiles.lst from ResourceManager");
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to load tile list for palette: {}", e.what());
        }
    }

    // Load objects into object palette panel
    if (_objectPalettePanel) {
        try {
            _objectPalettePanel->setMap(map);
            _objectPalettePanel->loadObjects();
            spdlog::info("Loaded objects into ObjectPalettePanel");
        } catch (const std::exception& e) {
            spdlog::error("Failed to load objects for palette: {}", e.what());
        }
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

void MainWindow::setupPanelsMenu() {
    // Map Info Panel
    _mapInfoPanelAction = _panelsMenu->addAction("Map &Information");
    _mapInfoPanelAction->setCheckable(true);
    _mapInfoPanelAction->setChecked(true);
    connect(_mapInfoPanelAction, &QAction::toggled, [this](bool visible) {
        spdlog::debug("Map Info Panel action toggled: {}", visible);
        if (visible) {
            _mapInfoDock->show();
            _mapInfoDock->raise();
        } else {
            _mapInfoDock->hide();
        }
    });

    // Selection Panel
    _selectionPanelAction = _panelsMenu->addAction("&Selection");
    _selectionPanelAction->setCheckable(true);
    _selectionPanelAction->setChecked(true);
    connect(_selectionPanelAction, &QAction::toggled, [this](bool visible) {
        spdlog::debug("Selection Panel action toggled: {}", visible);
        if (visible) {
            _selectionDock->show();
            _selectionDock->raise();
        } else {
            _selectionDock->hide();
        }
    });

    // Tile Palette Panel
    _tilePalettePanelAction = _panelsMenu->addAction("&Tile Palette");
    _tilePalettePanelAction->setCheckable(true);
    _tilePalettePanelAction->setChecked(true);
    connect(_tilePalettePanelAction, &QAction::toggled, [this](bool visible) {
        spdlog::debug("Tile Palette Panel action toggled: {}", visible);
        if (visible) {
            _tilePaletteDock->show();
            _tilePaletteDock->raise(); // Important: brings the tab to the front when tabified
        } else {
            _tilePaletteDock->hide();
        }
    });

    // Object Palette Panel
    _objectPalettePanelAction = _panelsMenu->addAction("&Object Palette");
    _objectPalettePanelAction->setCheckable(true);
    _objectPalettePanelAction->setChecked(true);
    connect(_objectPalettePanelAction, &QAction::toggled, [this](bool visible) {
        spdlog::debug("Object Palette Panel action toggled: {}", visible);
        if (visible) {
            _objectPaletteDock->show();
            _objectPaletteDock->raise();
        } else {
            _objectPaletteDock->hide();
        }
    });

    // File Browser Panel
    _fileBrowserPanelAction = _panelsMenu->addAction("&File Browser");
    _fileBrowserPanelAction->setCheckable(true);
    _fileBrowserPanelAction->setChecked(true);
    connect(_fileBrowserPanelAction, &QAction::toggled, [this](bool visible) {
        spdlog::debug("File Browser Panel action toggled: {}", visible);
        if (visible) {
            _fileBrowserDock->show();
            _fileBrowserDock->raise();
        } else {
            _fileBrowserDock->hide();
        }
    });

    // Connect dock widget visibility changes back to menu actions
    connect(_mapInfoDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Map Info Dock visibility changed: {}", visible);
        if (_mapInfoPanelAction && _mapInfoPanelAction->isChecked() != visible) {
            _mapInfoPanelAction->setChecked(visible);
        }
    });

    connect(_selectionDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Selection Dock visibility changed: {}", visible);
        if (_selectionPanelAction && _selectionPanelAction->isChecked() != visible) {
            _selectionPanelAction->setChecked(visible);
        }
    });

    connect(_tilePaletteDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Tile Palette Dock visibility changed: {}", visible);
        // For tabified dock widgets, we need to check if this dock is actually the current tab
        // Don't auto-uncheck the action when the dock becomes non-visible due to tab switching
        if (visible && _tilePalettePanelAction && !_tilePalettePanelAction->isChecked()) {
            _tilePalettePanelAction->setChecked(true);
        }
    });

    connect(_objectPaletteDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Object Palette Dock visibility changed: {}", visible);
        // For tabified dock widgets, we need to check if this dock is actually the current tab
        // Don't auto-uncheck the action when the dock becomes non-visible due to tab switching
        if (visible && _objectPalettePanelAction && !_objectPalettePanelAction->isChecked()) {
            _objectPalettePanelAction->setChecked(true);
        }
    });

    connect(_fileBrowserDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("File Browser Dock visibility changed: {}", visible);
        // For tabified dock widgets, we need to check if this dock is actually the current tab
        // Don't auto-uncheck the action when the dock becomes non-visible due to tab switching
        if (visible && _fileBrowserPanelAction && !_fileBrowserPanelAction->isChecked()) {
            _fileBrowserPanelAction->setChecked(true);
        }
    });

    spdlog::info("Panel management menu configured with bidirectional synchronization");
}

// Dock widget state management methods
void MainWindow::saveDockWidgetState() {
    QSettings settings("geck", "mapper");
    settings.setValue("dockWidgetState", saveState());
    settings.setValue("windowGeometry", saveGeometry());

    // Save individual floating dock widget geometries for better persistence
    settings.beginGroup("FloatingDockGeometries");

    if (_mapInfoDock->isFloating()) {
        settings.setValue("MapInfoDock", _mapInfoDock->saveGeometry());
    }
    if (_selectionDock->isFloating()) {
        settings.setValue("SelectionDock", _selectionDock->saveGeometry());
    }
    if (_tilePaletteDock->isFloating()) {
        settings.setValue("TilePaletteDock", _tilePaletteDock->saveGeometry());
    }
    if (_objectPaletteDock->isFloating()) {
        settings.setValue("ObjectPaletteDock", _objectPaletteDock->saveGeometry());
    }
    if (_fileBrowserDock->isFloating()) {
        settings.setValue("FileBrowserDock", _fileBrowserDock->saveGeometry());
    }

    settings.endGroup();
    spdlog::debug("Saved dock widget state, window geometry, and floating dock geometries");
}

void MainWindow::restoreDockWidgetState() {
    QSettings settings("geck", "mapper");

    // Restore window geometry first
    QByteArray geometry = settings.value("windowGeometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    // Restore dock widget state
    QByteArray state = settings.value("dockWidgetState").toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);

        // Restore individual floating dock widget geometries after a short delay
        // This ensures the dock widgets are fully initialized first
        QTimer::singleShot(100, this, [this]() {
            QSettings settings("geck", "mapper"); // Create new settings instance in timer callback
            settings.beginGroup("FloatingDockGeometries");

            QByteArray mapInfoGeometry = settings.value("MapInfoDock").toByteArray();
            if (!mapInfoGeometry.isEmpty() && _mapInfoDock->isFloating()) {
                _mapInfoDock->restoreGeometry(mapInfoGeometry);
            }

            QByteArray selectionGeometry = settings.value("SelectionDock").toByteArray();
            if (!selectionGeometry.isEmpty() && _selectionDock->isFloating()) {
                _selectionDock->restoreGeometry(selectionGeometry);
            }

            QByteArray tilePaletteGeometry = settings.value("TilePaletteDock").toByteArray();
            if (!tilePaletteGeometry.isEmpty() && _tilePaletteDock->isFloating()) {
                _tilePaletteDock->restoreGeometry(tilePaletteGeometry);
            }

            QByteArray objectPaletteGeometry = settings.value("ObjectPaletteDock").toByteArray();
            if (!objectPaletteGeometry.isEmpty() && _objectPaletteDock->isFloating()) {
                _objectPaletteDock->restoreGeometry(objectPaletteGeometry);
            }

            QByteArray fileBrowserGeometry = settings.value("FileBrowserDock").toByteArray();
            if (!fileBrowserGeometry.isEmpty() && _fileBrowserDock->isFloating()) {
                _fileBrowserDock->restoreGeometry(fileBrowserGeometry);
            }

            settings.endGroup();
            spdlog::debug("Restored floating dock widget geometries");
        });

        spdlog::debug("Restored dock widget state and window geometry");
    } else {
        // If no saved state, ensure default layout is applied
        restoreDefaultLayout();
        spdlog::debug("No saved state found, using default dock widget layout");
    }
}

void MainWindow::restoreDefaultLayout() {
    // Remove all dock widgets from their current positions
    removeDockWidget(_mapInfoDock);
    removeDockWidget(_selectionDock);
    removeDockWidget(_tilePaletteDock);
    removeDockWidget(_objectPaletteDock);
    removeDockWidget(_fileBrowserDock);

    // Restore default positions
    addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
    addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
    addDockWidget(Qt::LeftDockWidgetArea, _tilePaletteDock);
    addDockWidget(Qt::LeftDockWidgetArea, _objectPaletteDock);
    addDockWidget(Qt::LeftDockWidgetArea, _fileBrowserDock);

    // Restore default layout - vertical stacking on right, tabbed on left
    splitDockWidget(_mapInfoDock, _selectionDock, Qt::Vertical);
    tabifyDockWidget(_tilePaletteDock, _objectPaletteDock);
    tabifyDockWidget(_objectPaletteDock, _fileBrowserDock);

    // Ensure tile palette is the active tab
    _tilePaletteDock->raise();

    spdlog::debug("Restored default dock widget layout");
}

void MainWindow::updatePanelMenuActions() {
    spdlog::debug("Updating panel menu actions to reflect actual visibility states");

    if (_mapInfoPanelAction) {
        bool visible = _mapInfoDock->isVisible();
        _mapInfoPanelAction->setChecked(visible);
        spdlog::debug("Map Info Panel: visible={}, hidden={}", visible, _mapInfoDock->isHidden());
    }
    if (_selectionPanelAction) {
        bool visible = _selectionDock->isVisible();
        _selectionPanelAction->setChecked(visible);
        spdlog::debug("Selection Panel: visible={}, hidden={}", visible, _selectionDock->isHidden());
    }

    // For tabified dock widgets, a dock widget can be not hidden but not visible (when another tab is active)
    // We should check the dock widget is available to the user (not hidden), regardless of whether it's the active tab
    if (_tilePalettePanelAction) {
        bool available = !_tilePaletteDock->isHidden();
        _tilePalettePanelAction->setChecked(available);
        spdlog::debug("Tile Palette Panel: visible={}, hidden={}, available={}",
            _tilePaletteDock->isVisible(), _tilePaletteDock->isHidden(), available);
    }
    if (_objectPalettePanelAction) {
        bool available = !_objectPaletteDock->isHidden();
        _objectPalettePanelAction->setChecked(available);
        spdlog::debug("Object Palette Panel: visible={}, hidden={}, available={}",
            _objectPaletteDock->isVisible(), _objectPaletteDock->isHidden(), available);
    }
    if (_fileBrowserPanelAction) {
        bool available = !_fileBrowserDock->isHidden();
        _fileBrowserPanelAction->setChecked(available);
        spdlog::debug("File Browser Panel: visible={}, hidden={}, available={}",
            _fileBrowserDock->isVisible(), _fileBrowserDock->isHidden(), available);
    }

    spdlog::debug("Panel menu action sync completed");
}

} // namespace geck