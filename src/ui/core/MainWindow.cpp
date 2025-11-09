#define QT_NO_EMIT
#include "MainWindow.h"
#include "EditorWidget.h"
#include "../widgets/LoadingWidget.h"
#include "../widgets/WelcomeWidget.h"
#include "../widgets/SFMLWidget.h"
#include "../panels/SelectionPanel.h"
#include "../panels/MapInfoPanel.h"
#include "../panels/TilePalettePanel.h"
#include "../panels/ObjectPalettePanel.h"
#include "../panels/FileBrowserPanel.h"
#include "../tiles/TilePlacementManager.h"
#include "../tools/ExitGridPlacementManager.h"
#include "../dialogs/SettingsDialog.h"
#include "../dialogs/ProEditorDialog.h"
#include "../dialogs/AboutDialog.h"
#include "../../state/loader/MapLoader.h"
#include "../../selection/SelectionState.h"
#include "../../util/Types.h"
#include "../../util/ResourceManager.h"
#include "../../util/Settings.h"
#include "../../util/QtDialogs.h"
#include "../../util/PathUtils.h"
#include "../../util/ProHelper.h"
#include "../../reader/lst/LstReader.h"
#include "../../reader/ReaderFactory.h"
#include "../../format/lst/Lst.h"
#include "../../format/pro/Pro.h"
#include "../../writer/map/MapWriter.h"
#include "../../editor/Object.h"

#include <fstream>

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
#include <QMenu>
#include <QIcon>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QUrl>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QProcess>
#include <QDir>
#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>

namespace geck {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , _centralStack(nullptr)
    , _gameLoopTimer(new QTimer(this))
    , _currentEditorWidget(nullptr)
    , _welcomeWidget(nullptr)
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

    setWindowTitle("Gecko - Fallout 2 Map Editor");
    setMinimumSize(1024, 768);

    setupUI();

    // Restore dock widget state from previous session
    restoreDockWidgetState();

    // Capture the restored visibility and hide panels until a map is loaded
    snapshotPanelVisibility();
    hidePanelsForNoMap();

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

    // Set main window reference for palette access
    _currentEditorWidget->setMainWindow(this);

    // Initialize the editor widget
    _currentEditorWidget->init();

    // Connect signals
    connectToEditorWidget();
    
    // Sync current menu state to the new EditorWidget
    syncMenuStateToEditorWidget();

    if (_currentEditorWidget->getMap()) {
        updateMapInfo(_currentEditorWidget->getMap());
        restorePanelVisibilitySnapshot();
    } else {
        snapshotPanelVisibility();
        hidePanelsForNoMap();
    }

    QTimer::singleShot(50, this, &MainWindow::updatePanelMenuActions);
}


void MainWindow::setupUI() {
    // Create central stacked widget to hold loading and editor widgets
    _centralStack = new QStackedWidget(this);
    setCentralWidget(_centralStack);

    // Create and add welcome widget (shown when no map is loaded)
    _welcomeWidget = new WelcomeWidget(this);
    _centralStack->addWidget(_welcomeWidget);
    _centralStack->setCurrentWidget(_welcomeWidget);

    setupMenuBar();
    setupToolBar();
    setupDockWidgets();
    setupPanelsMenu();
    setupStatusBar();
    connectMenuSignals();
}

void MainWindow::connectMenuSignals() {
    // Connect MainWindow menu signals - these work regardless of EditorWidget state
    connect(this, &MainWindow::newMapRequested, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->createNewMap();
        } else {
            // Create new EditorWidget with empty map when no EditorWidget exists
            auto editorWidget = std::make_unique<EditorWidget>(nullptr);
            setEditorWidget(std::move(editorWidget));
            // Now create the new map
            _currentEditorWidget->createNewMap();
        }
    });
    connect(this, &MainWindow::openMapRequested, [this]() {
        if (_currentEditorWidget) {
            // Normal case: delegate to EditorWidget
            _currentEditorWidget->openMap();
        } else {
            // Fallback case: handle directly in MainWindow
            QString mapPath = QtDialogs::openMapFile(this, "Choose Fallout 2 map to load");
            if (!mapPath.isEmpty()) {
                handleMapLoadRequest(mapPath.toStdString(), true); // Force filesystem for File menu
            }
        }
    });
    connect(this, &MainWindow::saveMapRequested, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->saveMap();
        }
        // Note: Save Map should be disabled when no map is loaded
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
}

void MainWindow::setupMenuBar() {
    _menuBar = menuBar();

    // File Menu
    _fileMenu = _menuBar->addMenu("&File");

    QAction* newAction = _fileMenu->addAction(QIcon(":/icons/actions/new.svg"), "&New Map");
    newAction->setShortcut(QKeySequence::New);
    newAction->setStatusTip("Create a new map");
    connect(newAction, &QAction::triggered, this, &MainWindow::newMapRequested);

    QAction* openAction = _fileMenu->addAction(QIcon(":/icons/actions/open.svg"), "&Open Map");
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip("Open an existing map");
    connect(openAction, &QAction::triggered, this, &MainWindow::openMapRequested);

    QAction* saveAction = _fileMenu->addAction(QIcon(":/icons/actions/save.svg"), "&Save Map");
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setStatusTip("Save current map");
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveMapRequested);

    _fileMenu->addSeparator();
    
    QAction* preferencesAction = _fileMenu->addAction(QIcon(":/icons/actions/settings.svg"), "&Preferences...");
    preferencesAction->setShortcut(QKeySequence::Preferences);
    preferencesAction->setStatusTip("Open application preferences");
    connect(preferencesAction, &QAction::triggered, this, &MainWindow::showPreferences);

    _fileMenu->addSeparator();

    QAction* quitAction = _fileMenu->addAction(QIcon(":/icons/actions/quit.svg"), "&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setStatusTip("Exit the application");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // Edit Menu
    _editMenu = _menuBar->addMenu("&Edit");

    QAction* selectAllAction = _editMenu->addAction(QIcon(":/icons/actions/select-all.svg"), "Select &All");
    selectAllAction->setShortcut(QKeySequence("Ctrl+A"));
    selectAllAction->setStatusTip("Select all items of current type");
    connect(selectAllAction, &QAction::triggered, this, &MainWindow::selectAllRequested);

    QAction* deselectAllAction = _editMenu->addAction(QIcon(":/icons/actions/deselect.svg"), "&Deselect All");
    deselectAllAction->setShortcut(QKeySequence("Ctrl+D"));
    deselectAllAction->setStatusTip("Clear all selections");
    connect(deselectAllAction, &QAction::triggered, this, &MainWindow::deselectAllRequested);

    _editMenu->addSeparator();

    QAction* scrollBlockerRectangleAction = _editMenu->addAction(QIcon(":/icons/actions/scroll-blocker.svg"), "Scroll &Blocker Rectangle");
    scrollBlockerRectangleAction->setShortcut(QKeySequence("B"));
    scrollBlockerRectangleAction->setStatusTip("Draw rectangle and place scroll blockers on borders");
    connect(scrollBlockerRectangleAction, &QAction::triggered, this, &MainWindow::toggleScrollBlockerRectangleMode);

    // View Menu
    _viewMenu = _menuBar->addMenu("&View");

    _showObjectsAction = _viewMenu->addAction("Show &Objects");
    _showObjectsAction->setCheckable(true);
    _showObjectsAction->setChecked(UI::DEFAULT_SHOW_OBJECTS);
    connect(_showObjectsAction, &QAction::toggled, this, &MainWindow::showObjectsToggled);

    _showCrittersAction = _viewMenu->addAction("Show &Critters");
    _showCrittersAction->setCheckable(true);
    _showCrittersAction->setChecked(UI::DEFAULT_SHOW_CRITTERS);
    connect(_showCrittersAction, &QAction::toggled, this, &MainWindow::showCrittersToggled);

    _showWallsAction = _viewMenu->addAction("Show &Walls");
    _showWallsAction->setCheckable(true);
    _showWallsAction->setChecked(UI::DEFAULT_SHOW_WALLS);
    connect(_showWallsAction, &QAction::toggled, this, &MainWindow::showWallsToggled);

    _showRoofsAction = _viewMenu->addAction("Show &Roofs");
    _showRoofsAction->setCheckable(true);
    _showRoofsAction->setChecked(UI::DEFAULT_SHOW_ROOF);
    connect(_showRoofsAction, &QAction::toggled, this, &MainWindow::showRoofsToggled);

    _showScrollBlockersAction = _viewMenu->addAction("Show Scroll &Blockers");
    _showScrollBlockersAction->setCheckable(true);
    _showScrollBlockersAction->setChecked(UI::DEFAULT_SHOW_SCROLL_BLK);
    connect(_showScrollBlockersAction, &QAction::toggled, this, &MainWindow::showScrollBlockersToggled);

    _showWallBlockersAction = _viewMenu->addAction("Show &Wall Blockers");
    _showWallBlockersAction->setCheckable(true);
    _showWallBlockersAction->setChecked(UI::DEFAULT_SHOW_WALL_BLK);
    connect(_showWallBlockersAction, &QAction::toggled, this, &MainWindow::showWallBlockersToggled);

    _showHexGridAction = _viewMenu->addAction("Show &Hex Grid");
    _showHexGridAction->setCheckable(true);
    _showHexGridAction->setChecked(UI::DEFAULT_SHOW_HEX_GRID);
    connect(_showHexGridAction, &QAction::toggled, this, &MainWindow::showHexGridToggled);

    _showLightOverlaysAction = _viewMenu->addAction("Show &Light Overlays");
    _showLightOverlaysAction->setCheckable(true);
    _showLightOverlaysAction->setChecked(false);
    connect(_showLightOverlaysAction, &QAction::toggled, this, &MainWindow::showLightOverlaysToggled);

    _showExitGridsAction = _viewMenu->addAction("Show &Exit Grids");
    _showExitGridsAction->setCheckable(true);
    _showExitGridsAction->setChecked(false);
    _showExitGridsAction->setShortcut(QKeySequence("Ctrl+E"));
    _showExitGridsAction->setStatusTip("Show exit grid markers");
    connect(_showExitGridsAction, &QAction::toggled, this, &MainWindow::showExitGridsToggled);

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

    _elevation1Action = _elevationMenu->addAction("Elevation &1");
    _elevation1Action->setCheckable(true);
    _elevation1Action->setChecked(true);
    _elevation1Action->setData(ELEVATION_1);
    _elevation1Action->setDisabled(true);
    elevationGroup->addAction(_elevation1Action);
    connect(_elevation1Action, &QAction::triggered, [this]() { elevationChanged(ELEVATION_1); });

    _elevation2Action = _elevationMenu->addAction("Elevation &2");
    _elevation2Action->setCheckable(true);
    _elevation2Action->setData(ELEVATION_2);
    _elevation2Action->setDisabled(true);
    elevationGroup->addAction(_elevation2Action);
    connect(_elevation2Action, &QAction::triggered, [this]() { elevationChanged(ELEVATION_2); });

    _elevation3Action = _elevationMenu->addAction("Elevation &3");
    _elevation3Action->setCheckable(true);
    _elevation3Action->setData(ELEVATION_3);
    _elevation3Action->setDisabled(true);
    elevationGroup->addAction(_elevation3Action);
    connect(_elevation3Action, &QAction::triggered, [this]() { elevationChanged(ELEVATION_3); });
    
    // Help Menu
    _helpMenu = _menuBar->addMenu("&Help");
    QAction* aboutAction = _helpMenu->addAction("&About Gecko...");
    aboutAction->setStatusTip("Show information about the application");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::setupToolBar() {
    _mainToolBar = addToolBar("Main");
    _mainToolBar->setObjectName("MainToolBar");
    _mainToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    // File operations
    QAction* newAction = _mainToolBar->addAction(QIcon(":/icons/actions/new.svg"), "New");
    newAction->setStatusTip("Create a new map");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newMapRequested);

    QAction* openAction = _mainToolBar->addAction(QIcon(":/icons/actions/open.svg"), "Open");
    openAction->setStatusTip("Open an existing map");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openMapRequested);

    QAction* saveAction = _mainToolBar->addAction(QIcon(":/icons/actions/save.svg"), "Save");
    saveAction->setStatusTip("Save the current map");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveMapRequested);

    // Play game action
    QAction* playAction = _mainToolBar->addAction(QIcon(":/icons/actions/play.svg"), "Play");
    playAction->setStatusTip("Save and play the current map in Fallout 2");
    playAction->setShortcut(QKeySequence("F5"));
    connect(playAction, &QAction::triggered, this, &MainWindow::onPlayGame);

    _mainToolBar->addSeparator();

    // Selection mode action with dropdown menu  
    _selectionModeAction = _mainToolBar->addAction(QIcon(":/icons/actions/select.svg"), "All");
    _selectionModeAction->setToolTip("Select the current selection mode");
    
    // Create dropdown menu with all selection modes
    _selectionModeMenu = new QMenu(this);
    for (int i = 0; i < static_cast<int>(SelectionMode::NUM_SELECTION_TYPES); ++i) {
        SelectionMode mode = static_cast<SelectionMode>(i);
        QAction* action = _selectionModeMenu->addAction(selectionModeToString(mode));
        action->setData(static_cast<int>(mode));
        action->setCheckable(true);
        
        // Set "All" mode as checked by default
        if (mode == SelectionMode::ALL) {
            action->setChecked(true);
        }
        
        // Connect each menu action to update editor widget
        connect(action, &QAction::triggered, this, [this, mode]() {
            if (_currentEditorWidget) {
                _currentEditorWidget->setSelectionMode(mode);
                // Update action text to show current mode
                _selectionModeAction->setText(selectionModeToString(mode));
                
                // Update checkmarks in menu
                for (QAction* menuAction : _selectionModeMenu->actions()) {
                    menuAction->setChecked(menuAction->data().toInt() == static_cast<int>(mode));
                }
            }
        });
    }
    
    // Connect the main action to show the dropdown menu
    connect(_selectionModeAction, &QAction::triggered, this, [this]() {
        // Get the toolbar button widget for this action to position the menu correctly
        QWidget* actionWidget = _mainToolBar->widgetForAction(_selectionModeAction);
        if (actionWidget) {
            // Position menu below the button
            QPoint menuPos = actionWidget->mapToGlobal(QPoint(0, actionWidget->height()));
            _selectionModeMenu->exec(menuPos);
        }
    });

    _mainToolBar->addSeparator();

    // Rotate action
    QAction* rotateAction = _mainToolBar->addAction(QIcon(":/icons/actions/rotate.svg"), "Rotate");
    
    // Mark exits tool
    _markExitsAction = _mainToolBar->addAction(QIcon(":/icons/actions/door-exit.svg"), "Mark Exits");
    _markExitsAction->setStatusTip("Select exit grids to edit their properties");
    _markExitsAction->setCheckable(true);
    connect(_markExitsAction, &QAction::triggered, this, [this](bool checked) {
        if (_currentEditorWidget) {
            _currentEditorWidget->setMarkExitsMode(checked);
        }
    });
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
    
    // Connect file browser signals
    connect(_fileBrowserPanel, &FileBrowserPanel::fileDoubleClicked, this, [this](const QString& filePath) {
        // Check if it's a map file
        if (filePath.endsWith(".map", Qt::CaseInsensitive)) {
            spdlog::info("MainWindow: Opening map from file browser: {}", filePath.toStdString());
            
            // File browser always uses VFS loading (forceFilesystem = false)
            handleMapLoadRequest(filePath.toStdString(), false);
        } else if (isTextFile(filePath)) {
            spdlog::info("MainWindow: Opening text file from file browser: {}", filePath.toStdString());
            
            // Open text files with configured editor
            openTextFileWithEditor(filePath);
        } else {
            spdlog::debug("MainWindow: Unsupported file type double-clicked: {}", filePath.toStdString());
            QtDialogs::showInfo(this, "File Type Not Supported", 
                QString("File type not supported for opening: %1\n\nYou can export the file using the right-click context menu.").arg(filePath));
        }
    });

    addDockWidget(Qt::LeftDockWidgetArea, _fileBrowserDock);

    // Configure initial dock layout - vertical stacking instead of tabs
    splitDockWidget(_mapInfoDock, _selectionDock, Qt::Vertical);
    tabifyDockWidget(_tilePaletteDock, _objectPaletteDock);
    tabifyDockWidget(_objectPaletteDock, _fileBrowserDock);

    // Set minimum sizes for dock widgets - very small values for maximum flexibility
    _mapInfoDock->setMinimumSize(100, 50);
    _selectionDock->setMinimumSize(100, 50);
    _tilePaletteDock->setMinimumSize(100, 100);
    _objectPaletteDock->setMinimumSize(100, 100);
    _fileBrowserDock->setMinimumSize(100, 100);

    // Set maximum sizes to allow expansion
    _mapInfoDock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _selectionDock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _tilePaletteDock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _objectPaletteDock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _fileBrowserDock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    // Let Qt handle initial dock sizing automatically for better resize flexibility
    // Fixed resizeDocks() calls can interfere with user resize operations
    spdlog::debug("Dock widget setup completed with flexible sizing and state monitoring");

    // Set panels above the SFML widget to prevent redrawing issues
    // This is achieved by using QDockWidget's floating and layering features
    // Dock widgets are already rendered above the central widget by Qt's design
    spdlog::info("Dock widgets configured with proper z-order and panel features");
}

void MainWindow::setupStatusBar() {
    _statusBar = statusBar();

    // Create main status label for messages
    _statusLabel = new QLabel("Ready");
    _statusLabel->setMinimumWidth(200);
    _statusBar->addWidget(_statusLabel, 1); // Stretch to take available space

    // Create hex index label
    _hexIndexLabel = new QLabel("Hex: N/A");
    _hexIndexLabel->setMinimumWidth(80);
    _statusBar->addPermanentWidget(_hexIndexLabel);
}

void MainWindow::updateHexIndexDisplay(int hexIndex) {
    if (hexIndex >= 0) {
        _hexIndexLabel->setText(QString("Hex: %1").arg(hexIndex));
    } else {
        _hexIndexLabel->setText("Hex: N/A");
    }
}

void MainWindow::updateModeDisplay(const QString& modeText, const QString& iconPath) {
    // Update action text and icon for special modes like tile painting
    if (_selectionModeAction) {
        _selectionModeAction->setText(modeText);
        _selectionModeAction->setIcon(QIcon(iconPath));
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
    _suppressPanelPreferenceUpdates = true;
    _suppressPanelSnapshotUpdates = true;
    stopGameLoop();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Handle special shortcuts before forwarding to SFML
    if (event->key() == Qt::Key_P && _currentEditorWidget) {
        // Open PRO editor for selected object
        if (openProEditorForSelectedObject()) {
            event->accept();
            return;
        }
    }
    
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
        case Qt::Key_B:
            key = sf::Keyboard::Key::B;
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
    connect(this, &MainWindow::showWallBlockersToggled, [this](bool enabled) {
        _currentEditorWidget->setShowWallBlockers(enabled);
    });

    connect(this, &MainWindow::showHexGridToggled, [this](bool enabled) {
        _currentEditorWidget->setShowHexGrid(enabled);
    });

    connect(this, &MainWindow::showLightOverlaysToggled, [this](bool enabled) {
        _currentEditorWidget->setShowLightOverlays(enabled);
    });

    connect(this, &MainWindow::showExitGridsToggled, [this](bool enabled) {
        _currentEditorWidget->setShowExitGrids(enabled);
    });

    // Connect elevation changes
    connect(this, &MainWindow::elevationChanged, [this](int elevation) {
        _currentEditorWidget->changeElevation(elevation);
    });

    // Connect toolbar actions
    // Note: Selection mode is now handled directly by the combo box
    connect(this, &MainWindow::rotateObjectRequested, [this]() {
        _currentEditorWidget->rotateSelectedObject();
    });
    connect(this, &MainWindow::toggleScrollBlockerRectangleMode, [this]() {
        _currentEditorWidget->toggleScrollBlockerRectangleMode();
    });

    // Connect EditorWidget's selection signals to the unified SelectionPanel
    if (_selectionPanel) {
        // Set the map reference for the selection panel
        _selectionPanel->setMap(_currentEditorWidget->getMap());

        connect(_currentEditorWidget, &EditorWidget::selectionChanged, _selectionPanel, &SelectionPanel::handleSelectionChanged);

        // Connect selection changes to inventory panel for real-time updates
        connect(_currentEditorWidget, &EditorWidget::selectionChanged, this, [this](const geck::selection::SelectionState& selectionState) {
            auto selectedObjects = selectionState.getObjects();

            if (selectedObjects.size() == 1) {
                auto object = selectedObjects.front();
                if (object && object->hasMapObject()) {
                    auto mapObjectPtr = object->getMapObjectPtr();
                    if (mapObjectPtr && (mapObjectPtr->objects_in_inventory > 0 || !mapObjectPtr->inventory.empty())) {
                        // Object has inventory - update panel
                        return;
                    }
                }
            }

            // No inventory object selected - clear panel
        });
        connect(_selectionPanel, &SelectionPanel::objectFrmChanged, _currentEditorWidget, &EditorWidget::onObjectFrmChanged);
        connect(_selectionPanel, &SelectionPanel::objectFrmPathChanged, _currentEditorWidget, &EditorWidget::onObjectFrmPathChanged);
        connect(_selectionPanel, &SelectionPanel::statusMessage, this, &MainWindow::showStatusMessage);
        connect(_selectionPanel, &SelectionPanel::requestProEditor, [this](std::shared_ptr<Object> object) {
            if (object && object->hasMapObject()) {
                openProEditorForSelectedObject();
            }
        });
        
        connect(_selectionPanel, &SelectionPanel::requestExitGridEditor, [this](std::shared_ptr<Object> object) {
            if (object && object->hasMapObject()) {
                auto* exitGridManager = _currentEditorWidget->getExitGridPlacementManager();
                if (exitGridManager) {
                    auto mapObjectPtr = object->getMapObjectPtr();
                    if (mapObjectPtr && mapObjectPtr->isExitGridMarker()) {
                        exitGridManager->editExitGridProperties(mapObjectPtr);
                    }
                }
            }
        });
        
        
        // Connect highlight request signal
        connect(_selectionPanel, &SelectionPanel::requestObjectHighlight, 
                [this](std::shared_ptr<Object> object) {
                    if (_currentEditorWidget && object) {
                        // Get current selection state
                        auto* selectionManager = _currentEditorWidget->getSelectionManager();
                        auto& selectionState = selectionManager->getMutableSelection();
                        
                        // Check if object is already selected
                        selection::SelectedItem item;
                        item.type = selection::SelectionType::OBJECT;
                        item.data = object;
                        
                        if (!selectionState.hasItem(item)) {
                            // Object not selected, add it
                            selectionState.addItem(item);
                        }
                        
                        // Always notify the rendering system to refresh highlight
                        _currentEditorWidget->selectionChanged(selectionState, _currentEditorWidget->getCurrentElevation());
                        
                        // Force a complete render update to ensure highlighting is visible
                        if (_currentEditorWidget->getSFMLWidget()) {
                            _currentEditorWidget->getSFMLWidget()->update();
                            _currentEditorWidget->getSFMLWidget()->repaint();
                        }
                    }
                });
        
        spdlog::info("Connected EditorWidget selection signals to unified SelectionPanel");
    }
    
    // Connect status message signals
    connect(_currentEditorWidget, &EditorWidget::statusMessageRequested, this, &MainWindow::showStatusMessage);
    connect(_currentEditorWidget, &EditorWidget::statusMessageClearRequested, this, &MainWindow::clearStatusMessage);

    // Connect TilePalettePanel to EditorWidget
    if (_tilePalettePanel) {
        // Set the map reference for the tile palette panel
        _tilePalettePanel->setMap(_currentEditorWidget->getMap());
        
        // Set the selection manager reference for tile replacement detection
        _tilePalettePanel->setSelectionManager(_currentEditorWidget->getSelectionManager());

        // Connect tile selection to enable tile placement mode
        connect(_tilePalettePanel, &TilePalettePanel::tileSelected,
            [this](int tileIndex, bool isRoof) {
                if (tileIndex >= 0) {
                    // Use the roof state from the tile palette
                    _currentEditorWidget->setTilePlacementMode(true, tileIndex, isRoof);
                    // Update toolbar to show tile painting mode
                    updateModeDisplay("Mode: Tile painting", ":/icons/actions/paint.svg");
                } else {
                    // Disable tile placement mode
                    _currentEditorWidget->setTilePlacementMode(false);
                    // Reset toolbar to selection mode
                    updateModeDisplay("Mode: All", ":/icons/actions/select.svg");
                }
            });

        // Connect placement mode changes to update unified placement settings
        connect(_tilePalettePanel, &TilePalettePanel::placementModeChanged,
            [this]([[maybe_unused]] TilePalettePanel::PlacementMode mode) {
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

        spdlog::info("Connected TilePalettePanel to EditorWidget");
    }

    // Connect hex hover signal to status bar
    connect(_currentEditorWidget, &EditorWidget::hexHoverChanged, this, &MainWindow::updateHexIndexDisplay);

    // Connect map loading signal (from File menu, so force filesystem)
    connect(_currentEditorWidget, &EditorWidget::mapLoadRequested, this, [this](const std::string& mapPath) {
        handleMapLoadRequest(mapPath, true); // Force filesystem for File menu
    });
    
    // Connect player position selection
    if (_mapInfoPanel) {
        connect(_mapInfoPanel, &MapInfoPanel::selectPlayerPositionRequested, 
                _currentEditorWidget, &EditorWidget::enterPlayerPositionSelectionMode);
        connect(_mapInfoPanel, &MapInfoPanel::centerViewOnPlayerPositionRequested,
                _currentEditorWidget, &EditorWidget::centerViewOnPlayerPosition);
        
        // Connect elevation added/removed signals to update elevation menu
        connect(_mapInfoPanel, &MapInfoPanel::elevationAdded,
                this, [this](int elevation) {
                    updateElevationMenu(_currentEditorWidget->getMap());
                    
                    // If the added elevation is the current one, reload sprites
                    if (_currentEditorWidget->getCurrentElevation() == elevation) {
                        _currentEditorWidget->loadTileSprites();
                        spdlog::info("MainWindow: Reloaded sprites for newly added elevation {}", elevation);
                    }
                });
        connect(_mapInfoPanel, &MapInfoPanel::elevationRemoved,
                this, [this](int elevation) {
                    updateElevationMenu(_currentEditorWidget->getMap());
                    
                    // If the removed elevation was the current one, switch to an available elevation
                    if (_currentEditorWidget->getCurrentElevation() == elevation) {
                        // Find first available elevation and switch to it
                        auto* map = _currentEditorWidget->getMap();
                        if (map) {
                            uint32_t flags = map->getMapFile().header.flags;
                            if ((flags & 0x2) == 0) { // Elevation 1 available
                                elevationChanged(ELEVATION_1);
                            } else if ((flags & 0x4) == 0) { // Elevation 2 available  
                                elevationChanged(ELEVATION_2);
                            } else if ((flags & 0x8) == 0) { // Elevation 3 available
                                elevationChanged(ELEVATION_3);
                            }
                        }
                        spdlog::info("MainWindow: Switched away from removed elevation {}", elevation);
                    }
                });
        
        connect(_currentEditorWidget, &EditorWidget::playerPositionSelected, 
                this, [this](int hexPosition) {
                    if (_mapInfoPanel) {
                        // Update the MapInfo panel with the selected position
                        // The panel will update the underlying map data
                        _mapInfoPanel->setPlayerPosition(hexPosition);
                    }
                });
    }

    spdlog::info("Qt6 menu connected to EditorWidget");
}

void MainWindow::syncMenuStateToEditorWidget() {
    if (!_currentEditorWidget) {
        return;
    }

    // Sync all visibility toggle states from menu to EditorWidget
    _currentEditorWidget->setShowObjects(_showObjectsAction->isChecked());
    _currentEditorWidget->setShowCritters(_showCrittersAction->isChecked());
    _currentEditorWidget->setShowWalls(_showWallsAction->isChecked());
    _currentEditorWidget->setShowRoof(_showRoofsAction->isChecked());
    _currentEditorWidget->setShowScrollBlk(_showScrollBlockersAction->isChecked());
    _currentEditorWidget->setShowWallBlockers(_showWallBlockersAction->isChecked());
    _currentEditorWidget->setShowHexGrid(_showHexGridAction->isChecked());
    _currentEditorWidget->setShowLightOverlays(_showLightOverlaysAction->isChecked());
    _currentEditorWidget->setShowExitGrids(_showExitGridsAction->isChecked());
    
    // Sync selection mode from action to EditorWidget (defaults to ALL mode)
    if (_currentEditorWidget) {
        _currentEditorWidget->setSelectionMode(SelectionMode::ALL);
        if (_selectionModeAction) {
            _selectionModeAction->setText("All");
        }
        // Update checkmarks in menu to show ALL mode is selected
        if (_selectionModeMenu) {
            for (QAction* menuAction : _selectionModeMenu->actions()) {
                menuAction->setChecked(menuAction->data().toInt() == static_cast<int>(SelectionMode::ALL));
            }
        }
    }
}

void MainWindow::updateMapInfo(Map* map) {
    if (_mapInfoPanel) {
        _mapInfoPanel->setMap(map);
    }

    // Update elevation menu based on available elevations
    updateElevationMenu(map);

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

void MainWindow::updateElevationMenu(Map* map) {
    if (!map) {
        // No map loaded, disable all elevation actions
        _elevation1Action->setDisabled(true);
        _elevation2Action->setDisabled(true);
        _elevation3Action->setDisabled(true);
        return;
    }

    // Get map flags to determine available elevations
    uint32_t map_flags = map->getMapFile().header.flags;
    bool hasElevation1 = ((map_flags & 0x2) == 0);
    bool hasElevation2 = ((map_flags & 0x4) == 0);
    bool hasElevation3 = ((map_flags & 0x8) == 0);

    // Enable/disable elevation actions based on availability
    _elevation1Action->setEnabled(hasElevation1);
    _elevation2Action->setEnabled(hasElevation2);
    _elevation3Action->setEnabled(hasElevation3);

    // Ensure the first available elevation is selected
    if (hasElevation1) {
        _elevation1Action->setChecked(true);
    } else if (hasElevation2) {
        _elevation2Action->setChecked(true);
    } else if (hasElevation3) {
        _elevation3Action->setChecked(true);
    }

    spdlog::debug("Updated elevation menu: E1={}, E2={}, E3={}", 
                  hasElevation1, hasElevation2, hasElevation3);
}

void MainWindow::handleMapLoadRequest(const std::string& mapPath, bool forceFilesystem) {
    spdlog::info("MainWindow: Handling request to load map: {} (filesystem: {})", mapPath, forceFilesystem);

    // Create loading dialog
    auto loadingWidget = std::make_unique<LoadingWidget>(this);
    loadingWidget->setWindowTitle("Loading Map");

    // Use unified MapLoader with source context
    loadingWidget->addLoader(std::make_unique<MapLoader>(mapPath, -1, forceFilesystem, [this](auto map) {
        // Check if loading was successful
        if (map) {
            // When loading is complete, create new editor widget and switch to it
            auto editorWidget = std::make_unique<EditorWidget>(std::move(map));
            setEditorWidget(std::move(editorWidget));
        }
        // If map is null, error was already shown by MapLoader::onDone()
    }));

    // Show modal loading dialog
    loadingWidget->exec();
    
    spdlog::info("Map loading completed from MainWindow");
}

void MainWindow::setupPanelsMenu() {
    // Map Info Panel
    _mapInfoPanelAction = _panelsMenu->addAction("Map &Information");
    _mapInfoPanelAction->setCheckable(true);
    _mapInfoPanelAction->setChecked(true);
    connect(_mapInfoPanelAction, &QAction::toggled, [this](bool visible) {
        if (_suppressPanelPreferenceUpdates) {
            return;
        }
        spdlog::debug("Map Info Panel action toggled: {}", visible);
        if (visible) {
            _mapInfoDock->show();
            if (!_mapInfoDock->isFloating() && dockWidgetArea(_mapInfoDock) != Qt::NoDockWidgetArea) {
                _mapInfoDock->raise();
            }
        } else {
            _mapInfoDock->hide();
        }
        snapshotPanelVisibility();
        persistPanelPreference(_mapInfoDock, visible);
    });

    // Selection Panel
    _selectionPanelAction = _panelsMenu->addAction("&Selection");
    _selectionPanelAction->setCheckable(true);
    _selectionPanelAction->setChecked(true);
    connect(_selectionPanelAction, &QAction::toggled, [this](bool visible) {
        if (_suppressPanelPreferenceUpdates) {
            return;
        }
        spdlog::debug("Selection Panel action toggled: {}", visible);
        if (visible) {
            _selectionDock->show();
            if (!_selectionDock->isFloating() && dockWidgetArea(_selectionDock) != Qt::NoDockWidgetArea) {
                _selectionDock->raise();
            }
        } else {
            _selectionDock->hide();
        }
        snapshotPanelVisibility();
        persistPanelPreference(_selectionDock, visible);
    });

    // Tile Palette Panel
    _tilePalettePanelAction = _panelsMenu->addAction("&Tile Palette");
    _tilePalettePanelAction->setCheckable(true);
    _tilePalettePanelAction->setChecked(true);
    connect(_tilePalettePanelAction, &QAction::toggled, [this](bool visible) {
        if (_suppressPanelPreferenceUpdates) {
            return;
        }
        spdlog::debug("Tile Palette Panel action toggled: {}", visible);
        if (visible) {
            _tilePaletteDock->show();
            if (!_tilePaletteDock->isFloating() && dockWidgetArea(_tilePaletteDock) != Qt::NoDockWidgetArea) {
                _tilePaletteDock->raise();
            }
        } else {
            _tilePaletteDock->hide();
        }
        snapshotPanelVisibility();
        persistPanelPreference(_tilePaletteDock, visible);
    });

    // Object Palette Panel
    _objectPalettePanelAction = _panelsMenu->addAction("&Object Palette");
    _objectPalettePanelAction->setCheckable(true);
    _objectPalettePanelAction->setChecked(true);
    connect(_objectPalettePanelAction, &QAction::toggled, [this](bool visible) {
        if (_suppressPanelPreferenceUpdates) {
            return;
        }
        spdlog::debug("Object Palette Panel action toggled: {}", visible);
        if (visible) {
            _objectPaletteDock->show();
            if (!_objectPaletteDock->isFloating() && dockWidgetArea(_objectPaletteDock) != Qt::NoDockWidgetArea) {
                _objectPaletteDock->raise();
            }
        } else {
            _objectPaletteDock->hide();
        }
        snapshotPanelVisibility();
        persistPanelPreference(_objectPaletteDock, visible);
    });

    // File Browser Panel
    _fileBrowserPanelAction = _panelsMenu->addAction("&File Browser");
    _fileBrowserPanelAction->setCheckable(true);
    _fileBrowserPanelAction->setChecked(true);
    connect(_fileBrowserPanelAction, &QAction::toggled, [this](bool visible) {
        if (_suppressPanelPreferenceUpdates) {
            return;
        }
        spdlog::debug("File Browser Panel action toggled: {}", visible);
        if (visible) {
            _fileBrowserDock->show();
            if (!_fileBrowserDock->isFloating() && dockWidgetArea(_fileBrowserDock) != Qt::NoDockWidgetArea) {
                _fileBrowserDock->raise();
            }
        } else {
            _fileBrowserDock->hide();
        }
        snapshotPanelVisibility();
        persistPanelPreference(_fileBrowserDock, visible);
    });

    // Inventory Panel functionality integrated into Selection Panel

    // Connect dock widget visibility changes back to menu actions
    connect(_mapInfoDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Map Info Dock visibility changed: {}", visible);
        const bool dockVisible = _mapInfoDock->toggleViewAction()->isChecked();
        if (_mapInfoPanelAction && _mapInfoPanelAction->isChecked() != dockVisible) {
            QSignalBlocker blocker(*_mapInfoPanelAction);
            _mapInfoPanelAction->setChecked(dockVisible);
        }
        if (!_suppressPanelPreferenceUpdates) {
            snapshotPanelVisibility();
            persistPanelPreference(_mapInfoDock, dockVisible);
        }
    });

    connect(_selectionDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Selection Dock visibility changed: {}", visible);
        const bool dockVisible = _selectionDock->toggleViewAction()->isChecked();
        if (_selectionPanelAction && _selectionPanelAction->isChecked() != dockVisible) {
            QSignalBlocker blocker(*_selectionPanelAction);
            _selectionPanelAction->setChecked(dockVisible);
        }
        if (!_suppressPanelPreferenceUpdates) {
            snapshotPanelVisibility();
            persistPanelPreference(_selectionDock, dockVisible);
        }
    });

    connect(_tilePaletteDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Tile Palette Dock visibility changed: {}", visible);
        const bool dockVisible = _tilePaletteDock->toggleViewAction()->isChecked();
        if (_tilePalettePanelAction && _tilePalettePanelAction->isChecked() != dockVisible) {
            QSignalBlocker blocker(*_tilePalettePanelAction);
            _tilePalettePanelAction->setChecked(dockVisible);
        }
        if (!_suppressPanelPreferenceUpdates) {
            snapshotPanelVisibility();
            persistPanelPreference(_tilePaletteDock, dockVisible);
        }
    });

    connect(_objectPaletteDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("Object Palette Dock visibility changed: {}", visible);
        const bool dockVisible = _objectPaletteDock->toggleViewAction()->isChecked();
        if (_objectPalettePanelAction && _objectPalettePanelAction->isChecked() != dockVisible) {
            QSignalBlocker blocker(*_objectPalettePanelAction);
            _objectPalettePanelAction->setChecked(dockVisible);
        }
        if (!_suppressPanelPreferenceUpdates) {
            snapshotPanelVisibility();
            persistPanelPreference(_objectPaletteDock, dockVisible);
        }
    });

    connect(_fileBrowserDock, &QDockWidget::visibilityChanged, [this](bool visible) {
        spdlog::debug("File Browser Dock visibility changed: {}", visible);
        const bool dockVisible = _fileBrowserDock->toggleViewAction()->isChecked();
        if (_fileBrowserPanelAction && _fileBrowserPanelAction->isChecked() != dockVisible) {
            QSignalBlocker blocker(*_fileBrowserPanelAction);
            _fileBrowserPanelAction->setChecked(dockVisible);
        }
        if (!_suppressPanelPreferenceUpdates) {
            snapshotPanelVisibility();
            persistPanelPreference(_fileBrowserDock, dockVisible);
        }
    });
}

void MainWindow::saveDockWidgetState() {
    auto& settings = Settings::getInstance();
    settings.setDockState(saveState());
    settings.setWindowGeometry(saveGeometry());
    settings.setWindowMaximized(isMaximized());

    // Save individual floating dock widget geometries for better persistence
    if (_mapInfoDock->isFloating()) {
        settings.setFloatingDockGeometry("MapInfoDock", _mapInfoDock->saveGeometry());
    }
    if (_selectionDock->isFloating()) {
        settings.setFloatingDockGeometry("SelectionDock", _selectionDock->saveGeometry());
    }
    if (_tilePaletteDock->isFloating()) {
        settings.setFloatingDockGeometry("TilePaletteDock", _tilePaletteDock->saveGeometry());
    }
    if (_objectPaletteDock->isFloating()) {
        settings.setFloatingDockGeometry("ObjectPaletteDock", _objectPaletteDock->saveGeometry());
    }
    if (_fileBrowserDock->isFloating()) {
        settings.setFloatingDockGeometry("FileBrowserDock", _fileBrowserDock->saveGeometry());
    }

    settings.save();
    spdlog::debug("Saved dock widget state, window geometry, and floating dock geometries");
}

void MainWindow::restoreDockWidgetState() {
    auto& settings = Settings::getInstance();

    // Restore window geometry first
    QByteArray geometry = settings.getWindowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    
    // Restore window state (maximized/normal)
    if (settings.getWindowMaximized()) {
        setWindowState(Qt::WindowMaximized);
    }

    // Restore dock widget state
    QByteArray state = settings.getDockState();
    if (!state.isEmpty()) {
        restoreState(state);

        // Restore individual floating dock widget geometries after a short delay
        // This ensures the dock widgets are fully initialized first
        QTimer::singleShot(100, this, [this]() {
            auto& timerSettings = Settings::getInstance();

            QByteArray mapInfoGeometry = timerSettings.getFloatingDockGeometry("MapInfoDock");
            if (!mapInfoGeometry.isEmpty() && _mapInfoDock->isFloating()) {
                _mapInfoDock->restoreGeometry(mapInfoGeometry);
            }

            QByteArray selectionGeometry = timerSettings.getFloatingDockGeometry("SelectionDock");
            if (!selectionGeometry.isEmpty() && _selectionDock->isFloating()) {
                _selectionDock->restoreGeometry(selectionGeometry);
            }

            QByteArray tilePaletteGeometry = timerSettings.getFloatingDockGeometry("TilePaletteDock");
            if (!tilePaletteGeometry.isEmpty() && _tilePaletteDock->isFloating()) {
                _tilePaletteDock->restoreGeometry(tilePaletteGeometry);
            }

            QByteArray objectPaletteGeometry = timerSettings.getFloatingDockGeometry("ObjectPaletteDock");
            if (!objectPaletteGeometry.isEmpty() && _objectPaletteDock->isFloating()) {
                _objectPaletteDock->restoreGeometry(objectPaletteGeometry);
            }

            QByteArray fileBrowserGeometry = timerSettings.getFloatingDockGeometry("FileBrowserDock");
            if (!fileBrowserGeometry.isEmpty() && _fileBrowserDock->isFloating()) {
                _fileBrowserDock->restoreGeometry(fileBrowserGeometry);
            }

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

void MainWindow::setDockVisibility(QDockWidget* dock, QAction* action, bool visible) {
    if (!dock) {
        return;
    }

    const bool previousPreferenceSuppression = _suppressPanelPreferenceUpdates;
    _suppressPanelPreferenceUpdates = true;

    if (action) {
        QSignalBlocker blocker(*action);
        action->setChecked(visible);
    }

    dock->setVisible(visible);

    _suppressPanelPreferenceUpdates = previousPreferenceSuppression;
}

void MainWindow::persistPanelPreference(QDockWidget* dock, bool visible) {
    if (_suppressPanelPreferenceUpdates || !dock) {
        return;
    }

    // For tabified docks, visibilityChanged(false) can fire when the dock just loses focus.
    // Treat only truly hidden docks (isHidden == true) as user intent to hide.
    if (!visible && !dock->isHidden()) {
        return;
    }

    const bool isShown = !dock->isHidden();

    auto& settings = Settings::getInstance();
    const QString dockName = dock->objectName();
    if (dockName.isEmpty()) {
        return;
    }

    auto preference = settings.getPanelVisibilityPreference(dockName);
    if (!preference.has_value() || preference.value() != isShown) {
        settings.setPanelVisibilityPreference(dockName, isShown);
        settings.save();
    }
}

void MainWindow::snapshotPanelVisibility() {
    if (_suppressPanelSnapshotUpdates) {
        return;
    }

    auto isPanelEnabled = [](QAction* action, QDockWidget* dock) {
        if (action) {
            return action->isChecked();
        }
        return dock && dock->isVisible();
    };

    _panelVisibilitySnapshot[_mapInfoDock] = isPanelEnabled(_mapInfoPanelAction, _mapInfoDock);
    _panelVisibilitySnapshot[_selectionDock] = isPanelEnabled(_selectionPanelAction, _selectionDock);
    _panelVisibilitySnapshot[_tilePaletteDock] = isPanelEnabled(_tilePalettePanelAction, _tilePaletteDock);
    _panelVisibilitySnapshot[_objectPaletteDock] = isPanelEnabled(_objectPalettePanelAction, _objectPaletteDock);
    _panelVisibilitySnapshot[_fileBrowserDock] = isPanelEnabled(_fileBrowserPanelAction, _fileBrowserDock);
}

void MainWindow::restorePanelVisibilitySnapshot() {
    if (_panelVisibilitySnapshot.empty()) {
        snapshotPanelVisibility();
    }

    auto& settings = Settings::getInstance();

    auto applySnapshot = [&](QDockWidget* dock, QAction* action) {
        if (!dock) {
            return;
        }
        const auto it = _panelVisibilitySnapshot.find(dock);
        bool visible = (it != _panelVisibilitySnapshot.end()) ? it->second : !dock->isHidden();

        if (auto preference = settings.getPanelVisibilityPreference(dock->objectName()); preference.has_value()) {
            visible = preference.value();
        }
        setDockVisibility(dock, action, visible);
    };

    applySnapshot(_mapInfoDock, _mapInfoPanelAction);
    applySnapshot(_selectionDock, _selectionPanelAction);
    applySnapshot(_tilePaletteDock, _tilePalettePanelAction);
    applySnapshot(_objectPaletteDock, _objectPalettePanelAction);
    applySnapshot(_fileBrowserDock, _fileBrowserPanelAction);

    updatePanelMenuActions();
    snapshotPanelVisibility();
}

void MainWindow::hidePanelsForNoMap() {
    const bool previousSuppression = _suppressPanelSnapshotUpdates;
    const bool previousPreferenceSuppression = _suppressPanelPreferenceUpdates;
    _suppressPanelSnapshotUpdates = true;
    _suppressPanelPreferenceUpdates = true;

    setDockVisibility(_mapInfoDock, _mapInfoPanelAction, false);
    setDockVisibility(_selectionDock, _selectionPanelAction, false);
    setDockVisibility(_tilePaletteDock, _tilePalettePanelAction, false);
    setDockVisibility(_objectPaletteDock, _objectPalettePanelAction, false);
    setDockVisibility(_fileBrowserDock, _fileBrowserPanelAction, true);

    if (_fileBrowserDock) {
        _fileBrowserDock->raise();
    }

    _suppressPanelSnapshotUpdates = previousSuppression;
    _suppressPanelPreferenceUpdates = previousPreferenceSuppression;
}

void MainWindow::updatePanelMenuActions() {
    spdlog::debug("Updating panel menu actions to reflect actual visibility states");

    if (_mapInfoPanelAction) {
        const bool visible = !_mapInfoDock->isHidden();
        QSignalBlocker blocker(*_mapInfoPanelAction);
        _mapInfoPanelAction->setChecked(visible);
        spdlog::debug("Map Info Panel: visible={}, hidden={}", visible, _mapInfoDock->isHidden());
    }
    if (_selectionPanelAction) {
        const bool visible = !_selectionDock->isHidden();
        QSignalBlocker blocker(*_selectionPanelAction);
        _selectionPanelAction->setChecked(visible);
        spdlog::debug("Selection Panel: visible={}, hidden={}", visible, _selectionDock->isHidden());
    }

    // For tabified dock widgets, a dock widget can be not hidden but not visible (when another tab is active)
    // We should check the dock widget is available to the user (not hidden), regardless of whether it's the active tab
    if (_tilePalettePanelAction) {
        const bool available = !_tilePaletteDock->isHidden();
        QSignalBlocker blocker(*_tilePalettePanelAction);
        _tilePalettePanelAction->setChecked(available);
        spdlog::debug("Tile Palette Panel: visible={}, hidden={}, available={}",
            _tilePaletteDock->isVisible(), _tilePaletteDock->isHidden(), available);
    }
    if (_objectPalettePanelAction) {
        const bool available = !_objectPaletteDock->isHidden();
        QSignalBlocker blocker(*_objectPalettePanelAction);
        _objectPalettePanelAction->setChecked(available);
        spdlog::debug("Object Palette Panel: visible={}, hidden={}, available={}",
            _objectPaletteDock->isVisible(), _objectPaletteDock->isHidden(), available);
    }
    if (_fileBrowserPanelAction) {
        const bool available = !_fileBrowserDock->isHidden();
        QSignalBlocker blocker(*_fileBrowserPanelAction);
        _fileBrowserPanelAction->setChecked(available);
        spdlog::debug("File Browser Panel: visible={}, hidden={}, available={}",
            _fileBrowserDock->isVisible(), _fileBrowserDock->isHidden(), available);
    }

    spdlog::debug("Panel menu action sync completed");
}

void MainWindow::hideNonEssentialPanels() {
    spdlog::debug("Hiding non-essential panels (no map loaded)");
    snapshotPanelVisibility();
    hidePanelsForNoMap();
}

void MainWindow::refreshFileBrowser() {
    if (_fileBrowserPanel) {
        spdlog::debug("Refreshing file browser after data loading");
        _fileBrowserPanel->refreshFileList();
    }
}

void MainWindow::showFileBrowserPanel() {
    // Show and raise the dock widget containing the file browser
    _fileBrowserDock->show();
    _fileBrowserDock->raise();
    
    // Make the file browser tab active within the tabbed dock area
    // Since file browser is tabified with tile and object palettes, we need to ensure it's the active tab
    _fileBrowserDock->widget()->setFocus();

    snapshotPanelVisibility();
    
    spdlog::debug("File browser panel shown and raised");
}

void MainWindow::showPreferences() {
    SettingsDialog dialog(this);
    int result = dialog.exec();
    
    if (result == QDialog::Accepted) {
        // Settings were saved, we might need to reload resources if data paths changed
        spdlog::info("Settings dialog closed with changes");
        // TODO: Reload ResourceManager if data paths changed
    } else {
        spdlog::info("Settings dialog cancelled");
    }
    
    // Ensure file browser panel is visible after closing preferences
    showFileBrowserPanel();
}

void MainWindow::showAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::onPlayGame() {
    auto& settings = Settings::getInstance();
    
    // Check if game location is configured and valid
    if (!settings.isGameLocationValid()) {
        QtDialogs::showWarning(this, "Game Location Not Configured",
            "Fallout 2 game location is not configured or invalid.\n\n"
            "Please set up the game location in Preferences (File > Preferences > Game Location).");
        return;
    }
    
    // Check if a map is currently loaded
    if (!_currentEditorWidget) {
        QtDialogs::showWarning(this, "No Map Loaded",
            "No map is currently loaded. Please open or create a map before playing.");
        return;
    }
    
    // Get the current map
    const auto& mapFile = _currentEditorWidget->getMapFile();
    std::string mapFilename = _currentEditorWidget->getMap()->filename();
    
    // Ensure the filename has .map extension
    if (!mapFilename.ends_with(".map")) {
        mapFilename += ".map";
    }
    
    // Handle different installation types
    if (settings.getGameInstallationType() == Settings::GameInstallationType::STEAM) {
        // For Steam installations, we don't need to copy the map file
        // Steam will handle the game launch via App ID
        launchGameViaSteam(settings.getSteamAppId());
        return;
    }
    
    // Get game data directory for map copying and ddraw.ini modification
    std::filesystem::path gameDataDir = settings.getGameLocation(); // Returns data directory for executable installs
    if (gameDataDir.empty()) {
        QtDialogs::showError(this, "Play Failed", 
            "No game data directory configured. Please set the game data directory in Preferences.");
        return;
    }
    
    // Get executable location for launching
    std::filesystem::path executableLocation = settings.getExecutableGameLocation();
    if (executableLocation.empty()) {
        QtDialogs::showError(this, "Play Failed", 
            "No game executable configured. Please set the game executable in Preferences.");
        return;
    }
    
    std::filesystem::path mapsDir = gameDataDir / "data" / "maps";
    std::filesystem::path mapDestination = mapsDir / mapFilename;
    
    showStatusMessage(QString("Playing map: %1").arg(QString::fromStdString(mapFilename)));
    
    try {
        // 1. Save the current map to the game directory
        if (!std::filesystem::exists(mapsDir)) {
            std::filesystem::create_directories(mapsDir);
        }
        
        MapWriter mapWriter{ [](int32_t PID) {
            return ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(PID));
        } };
        mapWriter.openFile(mapDestination.string());
        
        if (!mapWriter.write(mapFile)) {
            QtDialogs::showError(this, "Save Failed", 
                QString("Failed to save map to game directory: %1").arg(QString::fromStdString(mapDestination.string())));
            return;
        }
        
        spdlog::info("Saved map to game directory: {} ({} bytes)", mapDestination.string(), mapWriter.getBytesWritten());
        
        // 2. Modify ddraw.ini
        std::filesystem::path ddrawIniPath = gameDataDir / "ddraw.ini";
        if (!modifyDdrawIni(ddrawIniPath, mapFilename)) {
            QtDialogs::showWarning(this, "Configuration Warning",
                "Map saved successfully, but failed to modify ddraw.ini. You may need to manually set the starting map.");
        }
        
        // 3. Launch the game
        launchGame(executableLocation);
        
    } catch (const std::exception& e) {
        QtDialogs::showError(this, "Play Failed", 
            QString("Failed to play map: %1").arg(e.what()));
        spdlog::error("Failed to play map: {}", e.what());
    }
}

bool MainWindow::isTextFile(const QString& filePath) const {
    static const QStringList textExtensions = {
        "cfg", "txt", "gam", "msg", "lst", "int", "ssl", "ini"
    };
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return textExtensions.contains(suffix);
}

void MainWindow::openTextFileWithEditor(const QString& vfsFilePath) {
    try {
        // Get editor configuration from settings
        auto& settings = Settings::getInstance();
        auto editorMode = settings.getTextEditorMode();
        QString customEditorPath = settings.getCustomEditorPath();
        
        spdlog::info("MainWindow: Opening text file with {} editor: {}", 
                    (editorMode == Settings::TextEditorMode::CUSTOM) ? "custom" : "system", 
                    vfsFilePath.toStdString());
        
        // Get file from VFS
        auto& resourceManager = ResourceManager::getInstance();
        auto vfs = resourceManager.getVFS();
        
        if (!vfs) {
            QtDialogs::showError(this, "Error", "Virtual file system not available");
            return;
        }
        
        // Prepare VFS path (needs leading slash)
        std::filesystem::path vfsPath = "/" / std::filesystem::path(vfsFilePath.toStdString());
        vfspp::FileInfo vfsFileInfo = PathUtils::createNormalizedFileInfo(vfsPath);
        
        // Open file in VFS
        vfspp::IFilePtr vfsFile = vfs->OpenFile(vfsFileInfo, vfspp::IFile::FileMode::Read);
        if (!vfsFile) {
            QtDialogs::showError(this, "Error", 
                QString("Failed to open file: %1").arg(vfsFilePath));
            return;
        }
        
        // Read file data
        size_t fileSize = vfsFile->Size();
        std::vector<uint8_t> buffer(fileSize);
        size_t bytesRead = vfsFile->Read(buffer.data(), fileSize);
        
        if (bytesRead != fileSize) {
            QtDialogs::showError(this, "Error", 
                QString("Failed to read complete file: %1").arg(vfsFilePath));
            return;
        }
        
        // Create temporary file with same extension
        QFileInfo fileInfo(vfsFilePath);
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempFileName = QString("%1_XXXXXX.%2")
            .arg(fileInfo.baseName())
            .arg(fileInfo.suffix());
        QString tempFilePath = tempDir + "/" + tempFileName;
        
        QTemporaryFile tempFile(tempFilePath);
        tempFile.setAutoRemove(false); // Keep file for editor to open
        
        if (!tempFile.open()) {
            QtDialogs::showError(this, "Error", 
                QString("Failed to create temporary file for: %1").arg(vfsFilePath));
            return;
        }
        
        // Write data to temporary file
        tempFile.write(reinterpret_cast<const char*>(buffer.data()), fileSize);
        tempFile.close();
        
        // Get the actual temporary file path
        QString actualTempPath = tempFile.fileName();
        
        bool opened = false;
        
        if (editorMode == Settings::TextEditorMode::CUSTOM && !customEditorPath.isEmpty()) {
            // Validate custom editor exists
            if (!QFile::exists(customEditorPath)) {
                QtDialogs::showError(this, "Editor Not Found", 
                    QString("Custom editor not found: %1\n\nFalling back to system default editor.").arg(customEditorPath));
                spdlog::warn("Custom editor not found: {}, falling back to system default", customEditorPath.toStdString());
                // Fall through to system default
            } else {
                // Use custom editor with QProcess
                QStringList arguments;
                arguments << actualTempPath;
                
                opened = QProcess::startDetached(customEditorPath, arguments);
                
                if (!opened) {
                    QtDialogs::showError(this, "Error", 
                        QString("Failed to start custom editor: %1\n\nFalling back to system default editor.").arg(customEditorPath));
                    spdlog::warn("Failed to start custom editor: {}, falling back to system default", customEditorPath.toStdString());
                    // Fall through to system default
                } else {
                    spdlog::info("MainWindow: Successfully opened file with custom editor: {} -> {}", 
                                customEditorPath.toStdString(), actualTempPath.toStdString());
                }
            }
        }
        
        // Use system default editor if custom failed or not configured
        if (!opened) {
            QUrl fileUrl = QUrl::fromLocalFile(actualTempPath);
            opened = QDesktopServices::openUrl(fileUrl);
            
            if (!opened) {
                QtDialogs::showError(this, "Error", 
                    QString("Failed to open file with any editor.\nTemporary file: %1").arg(actualTempPath));
                // Clean up the temp file if opening failed
                QFile::remove(actualTempPath);
            } else {
                spdlog::info("MainWindow: Successfully opened file with system default editor: {}", actualTempPath.toStdString());
            }
        }
        
    } catch (const std::exception& e) {
        QtDialogs::showError(this, "Error", 
            QString("Failed to open text file: %1").arg(e.what()));
    }
}

void MainWindow::showStatusMessage(const QString& message) {
    if (_statusLabel) {
        _statusLabel->setText(message);
    }
}

void MainWindow::clearStatusMessage() {
    if (_statusLabel) {
        _statusLabel->setText("Ready");
    }
}

void MainWindow::deselectMarkExitsMode() {
    if (_markExitsAction) {
        _markExitsAction->setChecked(false);
    }
}

bool MainWindow::modifyDdrawIni(const std::filesystem::path& ddrawIniPath, const std::string& mapFilename) {
    try {
        std::string content;
        bool foundMiscSection = false;
        bool foundStartingMap = false;
        std::string miscSection;
        
        // Read existing file if it exists
        if (std::filesystem::exists(ddrawIniPath)) {
            std::ifstream file(ddrawIniPath);
            if (!file.is_open()) {
                spdlog::error("Failed to open ddraw.ini for reading: {}", ddrawIniPath.string());
                return false;
            }
            
            std::string line;
            std::string currentSection;
            
            while (std::getline(file, line)) {
                // Check for section headers
                if (line.starts_with("[") && line.ends_with("]")) {
                    currentSection = line;
                    if (line == "[Misc]") {
                        foundMiscSection = true;
                    }
                }
                
                // Check for StartingMap in Misc section
                if (foundMiscSection && currentSection == "[Misc]" && 
                    (line.starts_with("StartingMap=") || line.starts_with(";StartingMap="))) {
                    foundStartingMap = true;
                    line = "StartingMap=" + mapFilename;
                }
                
                content += line + "\n";
            }
            file.close();
        }
        
        // If no [Misc] section found, add it
        if (!foundMiscSection) {
            content += "\n[Misc]\n";
            foundMiscSection = true;
        }
        
        // If no StartingMap found in [Misc] section, add it
        if (!foundStartingMap && foundMiscSection) {
            // Find the [Misc] section and add StartingMap after it
            size_t miscPos = content.find("[Misc]");
            if (miscPos != std::string::npos) {
                size_t nextSection = content.find("\n[", miscPos + 6);
                if (nextSection != std::string::npos) {
                    content.insert(nextSection, "StartingMap=" + mapFilename + "\n");
                } else {
                    content += "StartingMap=" + mapFilename + "\n";
                }
            }
        }
        
        // Write the modified content back
        std::ofstream outFile(ddrawIniPath);
        if (!outFile.is_open()) {
            spdlog::error("Failed to open ddraw.ini for writing: {}", ddrawIniPath.string());
            return false;
        }
        
        outFile << content;
        outFile.close();
        
        spdlog::info("Modified {}: set StartingMap to {}", ddrawIniPath.string(), mapFilename);
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to modify {}: {}", ddrawIniPath.string(), e.what());
        return false;
    }
}

void MainWindow::launchGame(const std::filesystem::path& gameLocation) {
#ifdef __APPLE__
    std::filesystem::path gameApp;
    
    // Check if the gameLocation itself is a .app bundle
    if (gameLocation.extension() == ".app" && std::filesystem::exists(gameLocation)) {
        gameApp = gameLocation;
        spdlog::info("Using configured .app bundle: {}", gameApp.string());
    } else {
        // Look for .app bundles inside the gameLocation directory
        std::vector<std::string> possibleApps = {
            "Fallout 2.app",
            "fallout2.app"
        };
        
        for (const auto& appName : possibleApps) {
            std::filesystem::path candidate = gameLocation / appName;
            if (std::filesystem::exists(candidate)) {
                gameApp = candidate;
                spdlog::info("Found .app bundle in directory: {}", gameApp.string());
                break;
            }
        }
    }
    
    if (gameApp.empty()) {
        // Try launching via Steam if no .app found
        QProcess* steamProcess = new QProcess(this);
        connect(steamProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [steamProcess](int exitCode, QProcess::ExitStatus exitStatus) {
                spdlog::info("Steam launch finished with exit code: {}", exitCode);
                steamProcess->deleteLater();
            });
        
        connect(steamProcess, &QProcess::errorOccurred,
            [this, steamProcess](QProcess::ProcessError error) {
                QtDialogs::showError(this, "Game Launch Error",
                    "Could not find Fallout 2.app and failed to launch via Steam.\n\n"
                    "Please ensure Fallout 2 is installed through Steam or as a .app bundle.");
                spdlog::error("Failed to launch game via Steam");
                steamProcess->deleteLater();
            });
        
        spdlog::info("Attempting to launch Fallout 2 via Steam");
        steamProcess->start("open", QStringList() << "steam://run/38410"); // Fallout 2 Steam App ID
        showStatusMessage("Launching Fallout 2 via Steam...");
        return;
    }
    
    spdlog::info("Launching game app: {}", gameApp.string());
    
    // Launch the app using 'open' command on macOS
    QProcess* gameProcess = new QProcess(this);
    
    connect(gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [gameProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::CrashExit) {
                spdlog::warn("Game process crashed with exit code: {}", exitCode);
            } else {
                spdlog::info("Game process finished with exit code: {}", exitCode);
            }
            gameProcess->deleteLater();
        });
    
    connect(gameProcess, &QProcess::errorOccurred,
        [this, gameProcess](QProcess::ProcessError error) {
            QString errorMsg;
            switch (error) {
                case QProcess::FailedToStart:
                    errorMsg = "Failed to start the game process";
                    break;
                case QProcess::Crashed:
                    errorMsg = "Game process crashed";
                    break;
                default:
                    errorMsg = "Unknown error occurred while running the game";
                    break;
            }
            QtDialogs::showError(this, "Game Launch Error", errorMsg);
            spdlog::error("Game process error: {}", errorMsg.toStdString());
            gameProcess->deleteLater();
        });
    
    // Use 'open' command to launch the .app bundle
    gameProcess->start("open", QStringList() << QString::fromStdString(gameApp.string()));
    
#else
    // Windows/Linux: Look for executable files
    std::vector<std::string> possibleExes = {
        "fallout2.exe",
        "Fallout2.exe",
        "fallout2HR.exe",
        "f2_res.exe",
        "fallout2",     // Linux executable
        "Fallout2"      // Linux executable
    };
    
    std::filesystem::path gameExecutable;
    for (const auto& exeName : possibleExes) {
        std::filesystem::path candidate = gameLocation / exeName;
        if (std::filesystem::exists(candidate)) {
            gameExecutable = candidate;
            break;
        }
    }
    
    if (gameExecutable.empty()) {
        QtDialogs::showError(this, "Game Executable Not Found",
            "Could not find Fallout 2 executable in the game directory.\n\n"
            "Looked for: fallout2.exe, Fallout2.exe, fallout2HR.exe, f2_res.exe, fallout2, Fallout2");
        return;
    }
    
    spdlog::info("Launching game: {}", gameExecutable.string());
    
    // Launch the game using QProcess
    QProcess* gameProcess = new QProcess(this);
    
    // Set working directory to game location
    gameProcess->setWorkingDirectory(QString::fromStdString(gameLocation.string()));
    
    // Connect to handle process events
    connect(gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [gameProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::CrashExit) {
                spdlog::warn("Game process crashed with exit code: {}", exitCode);
            } else {
                spdlog::info("Game process finished with exit code: {}", exitCode);
            }
            gameProcess->deleteLater();
        });
    
    connect(gameProcess, &QProcess::errorOccurred,
        [this, gameProcess](QProcess::ProcessError error) {
            QString errorMsg;
            switch (error) {
                case QProcess::FailedToStart:
                    errorMsg = "Failed to start the game process";
                    break;
                case QProcess::Crashed:
                    errorMsg = "Game process crashed";
                    break;
                default:
                    errorMsg = "Unknown error occurred while running the game";
                    break;
            }
            QtDialogs::showError(this, "Game Launch Error", errorMsg);
            spdlog::error("Game process error: {}", errorMsg.toStdString());
            gameProcess->deleteLater();
        });
    
    // Start the game process
    gameProcess->start(QString::fromStdString(gameExecutable.string()));
    
#endif
    
    if (!gameProcess->waitForStarted(5000)) {
        QtDialogs::showError(this, "Game Launch Failed", 
            "Failed to start the game within 5 seconds.");
        gameProcess->deleteLater();
        return;
    }
    
    showStatusMessage("Game launched successfully!");
    spdlog::info("Game launched successfully");
}

void MainWindow::launchGameViaSteam(const std::string& appId) {
    spdlog::info("Launching Fallout 2 via Steam with App ID: {}", appId);
    
    QProcess* steamProcess = new QProcess(this);
    
    connect(steamProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [steamProcess](int exitCode, [[maybe_unused]] QProcess::ExitStatus exitStatus) {
            spdlog::info("Steam launch finished with exit code: {}", exitCode);
            steamProcess->deleteLater();
        });
    
    connect(steamProcess, &QProcess::errorOccurred,
        [this, steamProcess, appId]([[maybe_unused]] QProcess::ProcessError error) {
            QString errorMsg = QString("Failed to launch Fallout 2 via Steam (App ID: %1).\n\n"
                                     "Please ensure Steam is installed and Fallout 2 is available in your Steam library.")
                                     .arg(QString::fromStdString(appId));
            QtDialogs::showError(this, "Steam Launch Error", errorMsg);
            spdlog::error("Failed to launch game via Steam: App ID {}", appId);
            steamProcess->deleteLater();
        });
    
    QString steamUrl = QString("steam://run/%1").arg(QString::fromStdString(appId));
    
#ifdef __APPLE__
    steamProcess->start("open", QStringList() << steamUrl);
#elif defined(_WIN32)
    steamProcess->start("cmd", QStringList() << "/c" << "start" << steamUrl);
#else
    // Linux - try xdg-open first, then steam directly
    steamProcess->start("xdg-open", QStringList() << steamUrl);
#endif
    
    showStatusMessage(QString("Launching Fallout 2 via Steam (App ID: %1)...").arg(QString::fromStdString(appId)));
}

bool MainWindow::openProEditorForSelectedObject() {
    if (!_currentEditorWidget) {
        return false;
    }
    
    auto* selectionManager = _currentEditorWidget->getSelectionManager();
    if (!selectionManager) {
        return false;
    }
    
    const auto& selectionState = selectionManager->getCurrentSelection();
    if (selectionState.isEmpty()) {
        spdlog::debug("MainWindow::openProEditorForSelectedObject() - no selection");
        return false;
    }
    
    // Find the first selected object
    for (const auto& item : selectionState.items) {
        if (item.isObject()) {
            auto selectedObject = item.getObject();
            if (!selectedObject) {
                continue;
            }
            
            // Get the MapObject which contains the PID
            if (!selectedObject->hasMapObject()) {
                spdlog::debug("MainWindow::openProEditorForSelectedObject() - selected object has no MapObject");
                continue;
            }
            
            auto& mapObject = selectedObject->getMapObject();
            
            // Get the PRO file path based on PID
            try {
                std::string proFileName = ProHelper::basePath(mapObject.pro_pid);
                spdlog::debug("MainWindow::openProEditorForSelectedObject() - opening PRO: {}", proFileName);
                
                // Use the same approach as FileBrowserPanel to open PRO editor
                auto& resourceManager = ResourceManager::getInstance();
                auto vfs = resourceManager.getVFS();
                
                if (!vfs) {
                    spdlog::error("MainWindow::openProEditorForSelectedObject() - VFS not available");
                    return false;
                }
                
                // Create path in VFS format
                std::filesystem::path vfsPath = std::filesystem::path("/") / proFileName;
                auto fileInfo = PathUtils::createNormalizedFileInfo(vfsPath);
                
                vfspp::IFilePtr file = vfs->OpenFile(fileInfo, vfspp::IFile::FileMode::Read);
                if (!file || !file->IsOpened()) {
                    spdlog::error("MainWindow::openProEditorForSelectedObject() - could not open PRO file: {}", proFileName);
                    return false;
                }
                
                // Read PRO file data
                std::vector<uint8_t> buffer(file->Size());
                size_t bytesRead = file->Read(buffer, file->Size());
                
                if (bytesRead != file->Size()) {
                    spdlog::error("MainWindow::openProEditorForSelectedObject() - failed to read PRO file completely");
                    return false;
                }
                
                // Create temporary file
                auto tempPath = std::filesystem::temp_directory_path() / ("temp_" + std::filesystem::path(proFileName).filename().string());
                
                std::ofstream tempFile(tempPath, std::ios::binary);
                tempFile.write(reinterpret_cast<const char*>(buffer.data()), bytesRead);
                tempFile.close();
                
                // Load PRO file
                auto pro = ReaderFactory::readFile<Pro>(tempPath);
                if (!pro) {
                    spdlog::error("MainWindow::openProEditorForSelectedObject() - could not parse PRO file");
                    std::filesystem::remove(tempPath);
                    return false;
                }
                
                // Create and show PRO editor dialog
                ProEditorDialog dialog(std::move(pro), this);
                dialog.exec();
                
                // Clean up temp file
                std::filesystem::remove(tempPath);
                
                return true;
                
            } catch (const std::exception& e) {
                spdlog::error("MainWindow::openProEditorForSelectedObject() - exception: {}", e.what());
                return false;
            }
        }
    }
    
    spdlog::debug("MainWindow::openProEditorForSelectedObject() - no object selected");
    return false;
}

} // namespace geck
