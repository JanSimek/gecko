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
#include "../UIConstants.h"
#include "../../resource/GameResources.h"
#include "../../state/loader/MapLoader.h"
#include "../../state/loader/DataPathLoader.h"
#include "../../selection/SelectionState.h"
#include "../../util/Types.h"
#include "../../util/Settings.h"
#include "../../util/QtDialogs.h"
#include "../../util/ProHelper.h"
#include "../../reader/lst/LstReader.h"
#include "../../reader/ReaderFactory.h"
#include "../../format/lst/Lst.h"
#include "../../format/pro/Pro.h"
#include "../../writer/map/MapWriter.h"
#include "../../editor/Object.h"
#include "../IconHelper.h"
#include "../GameEnums.h"

#include <fstream>
#include <functional>

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
#include <QStringList>
#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>

namespace geck {

MainWindow::MainWindow(std::shared_ptr<resource::GameResources> resources, QWidget* parent)
    : QMainWindow(parent)
    , _centralStack(nullptr)
    , _gameLoopTimer(new QTimer(this))
    , _resourcesShared(std::move(resources))
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
    setMinimumSize(ui::constants::dialog_sizes::MAIN_WINDOW_MIN_WIDTH, ui::constants::dialog_sizes::MAIN_WINDOW_MIN_HEIGHT);

    setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);
    setDockNestingEnabled(true);

    // Enable proper focus handling for dock widgets
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

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
    if (_currentEditorWidget) {
        _centralStack->removeWidget(_currentEditorWidget);
        _currentEditorWidget->deleteLater();
    }

    // Reset toolbar toggle states when switching maps
    deselectMarkExitsMode();

    _currentEditorWidget = editorWidget.release();
    _centralStack->addWidget(_currentEditorWidget);
    _centralStack->setCurrentWidget(_currentEditorWidget);

    // Set main window reference for palette access
    _currentEditorWidget->setMainWindow(this);

    // Initialize the editor widget
    _currentEditorWidget->init();

    // Connect signals
    connectToEditorWidget();
    connect(_currentEditorWidget, &EditorWidget::undoStackChanged, this, &MainWindow::updateUndoRedoActions);

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
    updateUndoRedoActions();
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
            auto editorWidget = std::make_unique<EditorWidget>(*_resourcesShared, nullptr);
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

    updateUndoRedoActions();
}

QAction* MainWindow::addPanelToggleAction(const QString& label, QDockWidget* dock, QAction*& actionRef) {
    auto showDock = [this](QDockWidget* targetDock, bool visible) {
        if (!targetDock) {
            return;
        }

        if (visible) {
            targetDock->show();
            if (!targetDock->isFloating() && dockWidgetArea(targetDock) != Qt::NoDockWidgetArea) {
                targetDock->raise();
            }
            return;
        }

        targetDock->hide();
    };

    actionRef = _panelsMenu->addAction(label);
    actionRef->setCheckable(true);
    actionRef->setChecked(true);
    QAction* action = actionRef;

    connect(action, &QAction::toggled, this, [this, dock, showDock](bool visible) {
        if (_suppressPanelPreferenceUpdates) {
            return;
        }

        spdlog::debug("{} action toggled: {}", dock->windowTitle().toStdString(), visible);
        showDock(dock, visible);
        snapshotPanelVisibility();
        persistPanelPreference(dock, visible);
    });

    connect(dock, &QDockWidget::visibilityChanged, this, [this, dock, action](bool visible) {
        if (!visible && !dock->isHidden()) {
            return;
        }

        spdlog::debug("{} visibility changed: {}", dock->windowTitle().toStdString(), visible);
        const bool dockVisible = dock->toggleViewAction()->isChecked();
        if (action && action->isChecked() != dockVisible) {
            QSignalBlocker blocker(*action);
            action->setChecked(dockVisible);
        }
        if (!_suppressPanelPreferenceUpdates) {
            snapshotPanelVisibility();
            persistPanelPreference(dock, dockVisible);
        }
    });

    return action;
}

std::array<QDockWidget*, 5> MainWindow::managedDocks() const {
    return { _mapInfoDock, _selectionDock, _tilePaletteDock, _objectPaletteDock, _fileBrowserDock };
}

std::array<MainWindow::DockActionPair, 5> MainWindow::managedDockActionPairs() const {
    return { {
        { _mapInfoDock, _mapInfoPanelAction },
        { _selectionDock, _selectionPanelAction },
        { _tilePaletteDock, _tilePalettePanelAction },
        { _objectPaletteDock, _objectPalettePanelAction },
        { _fileBrowserDock, _fileBrowserPanelAction },
    } };
}

void MainWindow::applyDefaultDockPlacements() {
    for (QDockWidget* dock : managedDocks()) {
        if (dock) {
            removeDockWidget(dock);
        }
    }

    struct DockPlacement {
        QDockWidget* dock;
        Qt::DockWidgetArea area;
    };

    const std::array<DockPlacement, 5> placements = { {
        { _mapInfoDock, Qt::RightDockWidgetArea },
        { _selectionDock, Qt::RightDockWidgetArea },
        { _tilePaletteDock, Qt::LeftDockWidgetArea },
        { _objectPaletteDock, Qt::LeftDockWidgetArea },
        { _fileBrowserDock, Qt::LeftDockWidgetArea },
    } };

    for (const DockPlacement& placement : placements) {
        if (placement.dock) {
            addDockWidget(placement.area, placement.dock);
        }
    }
}

void MainWindow::applyDefaultPanelDockLayout() {
    if (_mapInfoDock && _selectionDock) {
        splitDockWidget(_mapInfoDock, _selectionDock, Qt::Vertical);
    }
    if (_tilePaletteDock && _objectPaletteDock) {
        tabifyDockWidget(_tilePaletteDock, _objectPaletteDock);
    }
    if (_objectPaletteDock && _fileBrowserDock) {
        tabifyDockWidget(_objectPaletteDock, _fileBrowserDock);
    }
    if (_tilePaletteDock) {
        _tilePaletteDock->raise();
    }
}

void MainWindow::setupMenuBar() {
    _menuBar = menuBar();

    auto addMenuAction = [this](QMenu* menu, const QString& iconPath, const QString& text, auto slot, const QKeySequence& shortcut = QKeySequence(), const QString& statusTip = QString()) {
        QAction* action = menu->addAction(createIcon(iconPath), text);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
        }
        if (!statusTip.isEmpty()) {
            action->setStatusTip(statusTip);
        }
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    auto addViewToggleAction = [this](QAction*& actionRef, const QString& iconPath, const QString& text, bool checked, auto signal, const QString& statusTip = QString(), const QKeySequence& shortcut = QKeySequence()) {
        actionRef = _viewMenu->addAction(createIcon(iconPath), text);
        actionRef->setCheckable(true);
        actionRef->setChecked(checked);
        if (!statusTip.isEmpty()) {
            actionRef->setStatusTip(statusTip);
        }
        if (!shortcut.isEmpty()) {
            actionRef->setShortcut(shortcut);
        }
        connect(actionRef, &QAction::toggled, this, signal);
    };

    _fileMenu = _menuBar->addMenu("&File");
    addMenuAction(_fileMenu, ":/icons/actions/new.svg", "&New Map", &MainWindow::newMapRequested, QKeySequence::New, "Create a new map");
    addMenuAction(_fileMenu, ":/icons/actions/open.svg", "&Open Map", &MainWindow::openMapRequested, QKeySequence::Open, "Open an existing map");
    addMenuAction(_fileMenu, ":/icons/actions/save.svg", "&Save Map", &MainWindow::saveMapRequested, QKeySequence::Save, "Save current map");

    _fileMenu->addSeparator();
    addMenuAction(_fileMenu, ":/icons/actions/settings.svg", "&Preferences...", &MainWindow::showPreferences, QKeySequence::Preferences, "Open application preferences");

    _fileMenu->addSeparator();
    addMenuAction(_fileMenu, ":/icons/actions/quit.svg", "&Quit", &QWidget::close, QKeySequence::Quit, "Exit the application");

    _editMenu = _menuBar->addMenu("&Edit");
    addMenuAction(_editMenu, ":/icons/actions/select-all.svg", "Select &All", &MainWindow::selectAllRequested, QKeySequence("Ctrl+A"), "Select all items of current type");
    addMenuAction(_editMenu, ":/icons/actions/deselect.svg", "&Deselect All", &MainWindow::deselectAllRequested, QKeySequence("Ctrl+D"), "Clear all selections");

    _editMenu->addSeparator();
    addMenuAction(_editMenu, ":/icons/actions/scroll-blocker.svg", "Scroll &Blocker Rectangle", &MainWindow::toggleScrollBlockerRectangleMode, QKeySequence("B"), "Draw rectangle and place scroll blockers on borders");

    _editMenu->addSeparator();

    _undoAction = _editMenu->addAction("&Undo");
    _undoAction->setShortcut(QKeySequence::Undo);
    _undoAction->setStatusTip("Undo last edit");
    connect(_undoAction, &QAction::triggered, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->undoLastEdit();
            updateUndoRedoActions();
        }
    });

    _redoAction = _editMenu->addAction("&Redo");
    _redoAction->setShortcut(QKeySequence::Redo);
    _redoAction->setStatusTip("Redo last edit");
    connect(_redoAction, &QAction::triggered, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->redoLastEdit();
            updateUndoRedoActions();
        }
    });

    updateUndoRedoActions();

    _viewMenu = _menuBar->addMenu("&View");
    struct ViewToggleSpec {
        QAction** actionRef;
        const char* iconPath;
        const char* text;
        bool checked;
        void (MainWindow::*signal)(bool);
        QString statusTip;
        QKeySequence shortcut;
    };

    const std::array<ViewToggleSpec, 9> viewToggleSpecs = { {
        { &_showObjectsAction, ":/icons/actions/view-objects.svg", "Show &Objects", UI::DEFAULT_SHOW_OBJECTS, &MainWindow::showObjectsToggled, {}, {} },
        { &_showCrittersAction, ":/icons/actions/view-critters.svg", "Show &Critters", UI::DEFAULT_SHOW_CRITTERS, &MainWindow::showCrittersToggled, {}, {} },
        { &_showWallsAction, ":/icons/actions/view-walls.svg", "Show &Walls", UI::DEFAULT_SHOW_WALLS, &MainWindow::showWallsToggled, {}, {} },
        { &_showRoofsAction, ":/icons/actions/view-roofs.svg", "Show &Roofs", UI::DEFAULT_SHOW_ROOF, &MainWindow::showRoofsToggled, {}, {} },
        { &_showScrollBlockersAction, ":/icons/actions/view-scroll-blockers.svg", "Show Scroll &Blockers", UI::DEFAULT_SHOW_SCROLL_BLK, &MainWindow::showScrollBlockersToggled, {}, {} },
        { &_showWallBlockersAction, ":/icons/actions/view-wall-blockers.svg", "Show &Wall Blockers", UI::DEFAULT_SHOW_WALL_BLK, &MainWindow::showWallBlockersToggled, {}, {} },
        { &_showHexGridAction, ":/icons/actions/view-grid.svg", "Show &Hex Grid", UI::DEFAULT_SHOW_HEX_GRID, &MainWindow::showHexGridToggled, {}, {} },
        { &_showLightOverlaysAction, ":/icons/actions/view-light.svg", "Show &Light Overlays", false, &MainWindow::showLightOverlaysToggled, {}, {} },
        { &_showExitGridsAction, ":/icons/actions/view-exits.svg", "Show &Exit Grids", false, &MainWindow::showExitGridsToggled, "Show exit grid markers", QKeySequence("Ctrl+E") },
    } };

    for (const ViewToggleSpec& spec : viewToggleSpecs) {
        addViewToggleAction(*spec.actionRef, spec.iconPath, spec.text, spec.checked, spec.signal, spec.statusTip, spec.shortcut);
    }

    _viewMenu->addSeparator();

    _panelsMenu = _viewMenu->addMenu("&Panels");

    _viewMenu->addSeparator();

    QMenu* dockLayoutMenu = _viewMenu->addMenu("&Dock Layout");
    QAction* resetLayoutAction = dockLayoutMenu->addAction("&Reset to Default Layout");
    resetLayoutAction->setStatusTip("Reset all panels to their default positions");
    connect(resetLayoutAction, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);

    QAction* redockAllAction = dockLayoutMenu->addAction("Re-&dock All Floating Panels");
    redockAllAction->setStatusTip("Dock all floating panels back to the main window");
    connect(redockAllAction, &QAction::triggered, [this]() {
        for (QDockWidget* dock : { _mapInfoDock, _selectionDock, _tilePaletteDock, _objectPaletteDock, _fileBrowserDock }) {
            if (dock->isFloating()) {
                dock->setFloating(false);
            }
        }
        spdlog::info("Re-docked all floating panels");
    });

    dockLayoutMenu->addSeparator();

    QActionGroup* layoutGroup = new QActionGroup(this);

    auto addDockLayoutAction = [this, dockLayoutMenu, layoutGroup](const QString& text, const QString& statusTip, bool checked, auto applyLayout) {
        QAction* action = dockLayoutMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        action->setStatusTip(statusTip);
        layoutGroup->addAction(action);
        connect(action, &QAction::triggered, this, applyLayout);
        return action;
    };

    struct DockLayoutSpec {
        const char* text;
        const char* statusTip;
        bool checked;
        std::function<void()> applyLayout;
    };

    const std::array<DockLayoutSpec, 4> dockLayoutSpecs = { {
        { "&Vertical Stack (Right Side)", "Stack Map Info and Selection panels vertically on the right", true, [this]() {
            addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
            addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
            splitDockWidget(_mapInfoDock, _selectionDock, Qt::Vertical);
        } },
        { "&Horizontal Stack (Right Side)", "Stack Map Info and Selection panels horizontally on the right", false, [this]() {
            addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
            addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
            splitDockWidget(_mapInfoDock, _selectionDock, Qt::Horizontal);
        } },
        { "&Tabbed Layout (Right Side)", "Tab Map Info and Selection panels together on the right", false, [this]() {
            addDockWidget(Qt::RightDockWidgetArea, _mapInfoDock);
            addDockWidget(Qt::RightDockWidgetArea, _selectionDock);
            tabifyDockWidget(_mapInfoDock, _selectionDock);
            _mapInfoDock->raise();
        } },
        { "&Bottom Dock", "Move Map Info and Selection panels to the bottom area", false, [this]() {
            addDockWidget(Qt::BottomDockWidgetArea, _mapInfoDock);
            addDockWidget(Qt::BottomDockWidgetArea, _selectionDock);
            splitDockWidget(_mapInfoDock, _selectionDock, Qt::Horizontal);
        } },
    } };

    for (const DockLayoutSpec& spec : dockLayoutSpecs) {
        addDockLayoutAction(spec.text, spec.statusTip, spec.checked, spec.applyLayout);
    }

    _viewMenu->addSeparator();

    _elevationMenu = _viewMenu->addMenu("&Elevation");
    QActionGroup* elevationGroup = new QActionGroup(this);

    struct ElevationActionSpec {
        QAction** actionRef;
        const char* text;
        int elevation;
        bool checked;
    };

    const std::array<ElevationActionSpec, 3> elevationSpecs = { {
        { &_elevation1Action, "Elevation &1", ELEVATION_1, true },
        { &_elevation2Action, "Elevation &2", ELEVATION_2, false },
        { &_elevation3Action, "Elevation &3", ELEVATION_3, false },
    } };

    for (const ElevationActionSpec& spec : elevationSpecs) {
        QAction* action = _elevationMenu->addAction(spec.text);
        action->setCheckable(true);
        action->setChecked(spec.checked);
        action->setData(spec.elevation);
        action->setDisabled(true);
        elevationGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, elevation = spec.elevation]() { elevationChanged(elevation); });
        *spec.actionRef = action;
    }

    _helpMenu = _menuBar->addMenu("&Help");
    QAction* aboutAction = _helpMenu->addAction("&About Gecko...");
    aboutAction->setStatusTip("Show information about the application");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::setupToolBar() {
    _mainToolBar = addToolBar("Main");
    _mainToolBar->setObjectName("MainToolBar");
    _mainToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    auto addToolAction = [this](const QString& iconPath, const QString& text, auto slot, const QString& statusTip = QString(), const QKeySequence& shortcut = QKeySequence()) {
        QAction* action = _mainToolBar->addAction(createIcon(iconPath), text);
        if (!statusTip.isEmpty()) {
            action->setStatusTip(statusTip);
        }
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
        }
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    struct ToolbarActionSpec {
        const char* iconPath;
        const char* text;
        const char* statusTip;
        QKeySequence shortcut;
        std::function<void()> trigger;
    };

    const std::array<ToolbarActionSpec, 4> primaryToolbarActions = { {
        { ":/icons/actions/new.svg", "New", "Create a new map", {}, [this]() { newMapRequested(); } },
        { ":/icons/actions/open.svg", "Open", "Open an existing map", {}, [this]() { openMapRequested(); } },
        { ":/icons/actions/save.svg", "Save", "Save the current map", {}, [this]() { saveMapRequested(); } },
        { ":/icons/actions/play.svg", "Play", "Save and play the current map in Fallout 2", QKeySequence("F5"), [this]() { onPlayGame(); } },
    } };

    for (const ToolbarActionSpec& spec : primaryToolbarActions) {
        QAction* action = _mainToolBar->addAction(createIcon(spec.iconPath), spec.text);
        action->setStatusTip(spec.statusTip);
        if (!spec.shortcut.isEmpty()) {
            action->setShortcut(spec.shortcut);
        }
        connect(action, &QAction::triggered, this, [trigger = spec.trigger]() { trigger(); });
    }

    _mainToolBar->addSeparator(); // Separate play from selection controls

    // Selection mode action with dropdown menu
    _selectionModeAction = _mainToolBar->addAction(createIcon(":/icons/actions/select.svg"), "All");
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

    addToolAction(":/icons/actions/rotate.svg", "Rotate", &MainWindow::rotateObjectRequested, "Rotate selected object", QKeySequence("R"));

    // Mark exits tool
    _markExitsAction = _mainToolBar->addAction(createIcon(":/icons/actions/door-exit.svg"), "Mark Exits");
    _markExitsAction->setStatusTip("Select exit grids to edit their properties");
    _markExitsAction->setCheckable(true);
    connect(_markExitsAction, &QAction::triggered, this, [this](bool checked) {
        if (_currentEditorWidget) {
            _currentEditorWidget->setMarkExitsMode(checked);
        }
    });

    _mainToolBar->addSeparator();

    // Layer visibility toggles (reuse View menu actions)
    const std::array<QAction*, 9> layerVisibilityActions = {
        _showObjectsAction,
        _showCrittersAction,
        _showWallsAction,
        _showRoofsAction,
        _showHexGridAction,
        _showScrollBlockersAction,
        _showWallBlockersAction,
        _showLightOverlaysAction,
        _showExitGridsAction,
    };

    for (QAction* action : layerVisibilityActions) {
        _mainToolBar->addAction(action);
    }
}

void MainWindow::setupDockWidgets() {
    auto createDock = [this](const QString& title, const char* objectName, QWidget* panel, Qt::DockWidgetArea area, QSizePolicy::Policy verticalPolicy, int minHeight) {
        QDockWidget* dock = new QDockWidget(title, this);
        dock->setObjectName(objectName);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
        panel->setSizePolicy(QSizePolicy(QSizePolicy::Preferred, verticalPolicy));
        dock->setWidget(panel);
        addDockWidget(area, dock);
        dock->setMinimumSize(ui::constants::dock::MIN_WIDTH, minHeight);
        dock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        return dock;
    };

    _mapInfoPanel = new MapInfoPanel(*_resourcesShared);
    _mapInfoDock = createDock("Map Information", "MapInfoDock", _mapInfoPanel, Qt::RightDockWidgetArea, QSizePolicy::Preferred, ui::constants::dock::MIN_HEIGHT_SMALL);

    _selectionPanel = new SelectionPanel(*_resourcesShared);
    _selectionDock = createDock("Selection", "SelectionDock", _selectionPanel, Qt::RightDockWidgetArea, QSizePolicy::Preferred, ui::constants::dock::MIN_HEIGHT_SMALL);

    _tilePalettePanel = new TilePalettePanel(*_resourcesShared);
    _tilePaletteDock = createDock("Tile Palette", "TilePaletteDock", _tilePalettePanel, Qt::LeftDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_LARGE);

    _objectPalettePanel = new ObjectPalettePanel(*_resourcesShared);
    _objectPaletteDock = createDock("Object Palette", "ObjectPaletteDock", _objectPalettePanel, Qt::LeftDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_LARGE);

    _fileBrowserPanel = new FileBrowserPanel(_resourcesShared);
    _fileBrowserDock = createDock("Virtual File System Browser", "FileBrowserDock", _fileBrowserPanel, Qt::LeftDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_LARGE);
    connectFileBrowserSignals();

    applyDefaultPanelDockLayout();

    // Let Qt handle initial dock sizing automatically for better resize flexibility
    // Fixed resizeDocks() calls can interfere with user resize operations
    spdlog::debug("Dock widget setup completed with flexible sizing and state monitoring");

    // Set panels above the SFML widget to prevent redrawing issues
    // This is achieved by using QDockWidget's floating and layering features
    // Dock widgets are already rendered above the central widget by Qt's design
    spdlog::info("Dock widgets configured with proper z-order and panel features");
}

void MainWindow::connectFileBrowserSignals() {
    if (!_fileBrowserPanel) {
        return;
    }

    connect(_fileBrowserPanel, &FileBrowserPanel::fileDoubleClicked, this, [this](const QString& filePath) {
        if (filePath.endsWith(".map", Qt::CaseInsensitive)) {
            spdlog::info("MainWindow: Opening map from file browser: {}", filePath.toStdString());
            handleMapLoadRequest(filePath.toStdString(), false);
        } else if (isTextFile(filePath)) {
            spdlog::info("MainWindow: Opening text file from file browser: {}", filePath.toStdString());
            openTextFileWithEditor(filePath);
        } else {
            spdlog::debug("MainWindow: Unsupported file type double-clicked: {}", filePath.toStdString());
            QtDialogs::showInfo(this, "File Type Not Supported",
                QString("File type not supported for opening: %1\n\nYou can export the file using the right-click context menu.").arg(filePath));
        }
    });
}

void MainWindow::replaceDockPanelWidget(QDockWidget* dock, QWidget* panel, QSizePolicy::Policy verticalPolicy) {
    if (!dock || !panel) {
        return;
    }

    panel->setSizePolicy(QSizePolicy(QSizePolicy::Preferred, verticalPolicy));

    if (QWidget* oldWidget = dock->widget()) {
        oldWidget->hide();
        oldWidget->setParent(nullptr);
        delete oldWidget;
    }

    dock->setWidget(panel);
}

void MainWindow::rebuildResourcePanels() {
    _mapInfoPanel = new MapInfoPanel(*_resourcesShared);
    replaceDockPanelWidget(_mapInfoDock, _mapInfoPanel, QSizePolicy::Preferred);

    _selectionPanel = new SelectionPanel(*_resourcesShared);
    replaceDockPanelWidget(_selectionDock, _selectionPanel, QSizePolicy::Preferred);

    _tilePalettePanel = new TilePalettePanel(*_resourcesShared);
    replaceDockPanelWidget(_tilePaletteDock, _tilePalettePanel, QSizePolicy::Expanding);

    _objectPalettePanel = new ObjectPalettePanel(*_resourcesShared);
    replaceDockPanelWidget(_objectPaletteDock, _objectPalettePanel, QSizePolicy::Expanding);

    _fileBrowserPanel = new FileBrowserPanel(_resourcesShared);
    replaceDockPanelWidget(_fileBrowserDock, _fileBrowserPanel, QSizePolicy::Expanding);
    connectFileBrowserSignals();
}

void MainWindow::rebuildGameResourcesFromSettings() {
    const auto dataPaths = Settings::getInstance().getDataPaths();

    if (hasActiveMap()) {
        closeCurrentMap();
    }

    auto newResources = std::make_shared<resource::GameResources>();
    auto loadingWidget = std::make_unique<LoadingWidget>(this);
    loadingWidget->setWindowTitle("Reloading Game Data");

    auto loader = std::make_unique<DataPathLoader>(newResources, dataPaths);
    DataPathLoader* loaderPtr = loader.get();
    loadingWidget->addLoader(std::move(loader));
    loadingWidget->exec();

    const bool loaderHadErrors = loaderPtr && loaderPtr->hasError();
    const std::string loaderErrorMessage = loaderPtr ? loaderPtr->errorMessage() : std::string();

    _resourcesShared = std::move(newResources);
    rebuildResourcePanels();
    refreshFileBrowser();
    showFileBrowserPanel();
    updatePanelMenuActions();

    if (loaderHadErrors) {
        QtDialogs::showWarning(this, "Game Data Reloaded With Errors",
            QString::fromStdString(loaderErrorMessage.empty() ? "Some data paths failed to load." : loaderErrorMessage));
    }
}

void MainWindow::setupStatusBar() {
    _statusBar = statusBar();

    // Create main status label for messages
    _statusLabel = new QLabel("Ready");
    _statusLabel->setMinimumWidth(ui::constants::sizes::WIDTH_STATUS_LABEL);
    _statusBar->addWidget(_statusLabel, 1); // Stretch to take available space

    // Create hex index label
    _hexIndexLabel = new QLabel("Hex: N/A");
    _hexIndexLabel->setMinimumWidth(ui::constants::sizes::WIDTH_HEX_LABEL);
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
        _selectionModeAction->setIcon(themedIcon(iconPath));
    }
    updateUndoRedoActions();
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
                _mapInfoPanel->setPlayerPosition(hexPosition, _currentEditorWidget->getCurrentElevation());
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
    updateUndoRedoActions();

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
        // Load tile list from the shared resource repository
        try {
            const auto* tileList = _resourcesShared->repository().find<Lst>("art/tiles/tiles.lst");

            if (tileList) {
                _tilePalettePanel->loadTiles(tileList);
                spdlog::info("Loaded tiles.lst into palette: {} tiles", tileList->list().size());
            } else {
                spdlog::error("Failed to get tiles.lst from the resource repository");
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
    loadingWidget->addLoader(std::make_unique<MapLoader>(_resourcesShared, mapPath, -1, forceFilesystem, [this](auto map) {
        // Check if loading was successful
        if (map) {
            // When loading is complete, create new editor widget and switch to it
            auto editorWidget = std::make_unique<EditorWidget>(*_resourcesShared, std::move(map));
            setEditorWidget(std::move(editorWidget));
        }
        // If map is null, error was already shown by MapLoader::onDone()
    }));

    // Show modal loading dialog
    loadingWidget->exec();

    spdlog::info("Map loading completed from MainWindow");
}

void MainWindow::setupPanelsMenu() {
    struct PanelToggleSpec {
        const char* label;
        QDockWidget* dock;
        QAction** actionRef;
    };

    const std::array<PanelToggleSpec, 5> panelToggleSpecs = { {
        { "Map &Information", _mapInfoDock, &_mapInfoPanelAction },
        { "&Selection", _selectionDock, &_selectionPanelAction },
        { "&Tile Palette", _tilePaletteDock, &_tilePalettePanelAction },
        { "&Object Palette", _objectPaletteDock, &_objectPalettePanelAction },
        { "&Virtual File System Browser", _fileBrowserDock, &_fileBrowserPanelAction },
    } };

    for (const PanelToggleSpec& spec : panelToggleSpecs) {
        addPanelToggleAction(spec.label, spec.dock, *spec.actionRef);
    }
}

void MainWindow::saveDockWidgetState() {
    auto& settings = Settings::getInstance();
    settings.setDockState(saveState());
    settings.setWindowGeometry(saveGeometry());
    settings.setWindowMaximized(isMaximized());

    for (QDockWidget* dock : managedDocks()) {
        if (dock && dock->isFloating()) {
            settings.setFloatingDockGeometry(dock->objectName(), dock->saveGeometry());
        }
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

            for (QDockWidget* dock : managedDocks()) {
                if (!dock || !dock->isFloating()) {
                    continue;
                }

                const QByteArray geometry = timerSettings.getFloatingDockGeometry(dock->objectName());
                if (!geometry.isEmpty()) {
                    dock->restoreGeometry(geometry);
                }
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
    applyDefaultDockPlacements();
    applyDefaultPanelDockLayout();

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

    for (const auto& [dock, action] : managedDockActionPairs()) {
        if (dock) {
            _panelVisibilitySnapshot[dock] = isPanelEnabled(action, dock);
        }
    }
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

    for (const auto& [dock, action] : managedDockActionPairs()) {
        applySnapshot(dock, action);
    }

    updatePanelMenuActions();
    snapshotPanelVisibility();
}

void MainWindow::hidePanelsForNoMap() {
    const bool previousSuppression = _suppressPanelSnapshotUpdates;
    const bool previousPreferenceSuppression = _suppressPanelPreferenceUpdates;
    _suppressPanelSnapshotUpdates = true;
    _suppressPanelPreferenceUpdates = true;

    for (const auto& [dock, action] : managedDockActionPairs()) {
        setDockVisibility(dock, action, dock == _fileBrowserDock);
    }

    if (_fileBrowserDock) {
        _fileBrowserDock->raise();
    }

    _suppressPanelSnapshotUpdates = previousSuppression;
    _suppressPanelPreferenceUpdates = previousPreferenceSuppression;
}

void MainWindow::updatePanelMenuActions() {
    spdlog::debug("Updating panel menu actions to reflect actual visibility states");

    for (const auto& [dock, action] : managedDockActionPairs()) {
        if (!dock || !action) {
            continue;
        }

        const bool visible = !dock->isHidden();
        QSignalBlocker blocker(*action);
        action->setChecked(visible);
        spdlog::debug("{}: visible={}, hidden={}", dock->windowTitle().toStdString(), visible, dock->isHidden());
    }

    updateUndoRedoActions();

    spdlog::debug("Panel menu action sync completed");
}

void MainWindow::updateUndoRedoActions() {
    if (!_undoAction || !_redoAction) {
        return;
    }
    if (_currentEditorWidget) {
        const auto& stack = _currentEditorWidget->getUndoStack();
        bool canUndo = stack.canUndo();
        bool canRedo = stack.canRedo();
        std::string undoLabel = stack.lastUndoLabel();
        std::string redoLabel = stack.lastRedoLabel();
        _undoAction->setEnabled(canUndo);
        _redoAction->setEnabled(canRedo);
        _undoAction->setText(undoLabel.empty() ? "&Undo" : QString("Undo %1").arg(QString::fromStdString(undoLabel)));
        _redoAction->setText(redoLabel.empty() ? "&Redo" : QString("Redo %1").arg(QString::fromStdString(redoLabel)));
    } else {
        _undoAction->setEnabled(false);
        _redoAction->setEnabled(false);
        _undoAction->setText("&Undo");
        _redoAction->setText("&Redo");
    }
}

QIcon MainWindow::themedIcon(const QString& iconPath) const {
    QIcon baseIcon(iconPath);
    QPixmap pixmap = baseIcon.pixmap(24, 24);
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), palette().color(QPalette::WindowText));
    painter.end();
    return QIcon(pixmap);
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
    updateUndoRedoActions();
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

void MainWindow::closeCurrentMap() {
    if (!_currentEditorWidget) {
        return;
    }

    spdlog::info("Closing current map due to data path changes");

    // Stop the game loop
    stopGameLoop();

    // Remove the current editor widget from the stack
    _centralStack->removeWidget(_currentEditorWidget);

    // Delete the editor widget
    _currentEditorWidget->deleteLater();
    _currentEditorWidget = nullptr;

    // Show the welcome widget
    _centralStack->setCurrentWidget(_welcomeWidget);

    // Hide panels that are only relevant when a map is loaded
    hidePanelsForNoMap();

    spdlog::debug("Current map closed successfully");
    updateUndoRedoActions();
}

bool MainWindow::hasActiveMap() const {
    return _currentEditorWidget != nullptr;
}

void MainWindow::showPreferences() {
    SettingsDialog dialog(this);

    connect(&dialog, &SettingsDialog::settingsSaved, this, [this](bool dataPathsChanged) {
        if (dataPathsChanged) {
            rebuildGameResourcesFromSettings();
        }
    });

    int result = dialog.exec();

    if (result == QDialog::Accepted) {
        spdlog::info("Settings dialog closed with changes");
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

        MapWriter mapWriter{ [this](int32_t PID) {
            return _resourcesShared->repository().load<Pro>(ProHelper::basePath(*_resourcesShared, PID));
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
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return game::enums::textFileExtensions().contains(suffix);
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

        auto fileData = _resourcesShared->files().readRawBytes(vfsFilePath.toStdString());
        if (!fileData) {
            QtDialogs::showError(this, "Error",
                QString("Failed to open file: %1").arg(vfsFilePath));
            return;
        }

        // Create temporary file with same extension (if needed)
        QFileInfo fileInfo(vfsFilePath);
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempFileName = QString("%1_XXXXXX.%2")
                                   .arg(fileInfo.baseName())
                                   .arg(fileInfo.suffix());
        QString tempFilePath = tempDir + "/" + tempFileName;

        QString targetFilePath;
        bool usedTemporaryFile = false;

        // Try the original path from the browser first
        QFileInfo requestedPathInfo(vfsFilePath);
        if (requestedPathInfo.exists() && requestedPathInfo.isReadable() && requestedPathInfo.isFile()) {
            targetFilePath = requestedPathInfo.absoluteFilePath();
            spdlog::info("MainWindow: Opening native file directly at {}", targetFilePath.toStdString());
        } else {
            QTemporaryFile tempFile(tempFilePath);
            tempFile.setAutoRemove(false);

            if (!tempFile.open()) {
                QtDialogs::showError(this, "Error",
                    QString("Failed to create temporary file for: %1").arg(vfsFilePath));
                return;
            }

            tempFile.write(reinterpret_cast<const char*>(fileData->data()), static_cast<qsizetype>(fileData->size()));
            tempFile.close();

            targetFilePath = tempFile.fileName();
            usedTemporaryFile = true;
        }

        if (targetFilePath.isEmpty()) {
            QtDialogs::showError(this, "Error",
                QString("Failed to resolve path for: %1").arg(vfsFilePath));
            return;
        }

        bool opened = false;
        bool customAttempted = false;

        if (editorMode == Settings::TextEditorMode::CUSTOM && !customEditorPath.isEmpty()) {
            customAttempted = true;
            QStringList arguments;
            arguments << targetFilePath;

            opened = QProcess::startDetached(customEditorPath, arguments);

            if (opened) {
                spdlog::info("MainWindow: Successfully opened file with custom editor: {} -> {}",
                    customEditorPath.toStdString(), targetFilePath.toStdString());
            } else {
                spdlog::warn("MainWindow: Failed to start custom editor: {}", customEditorPath.toStdString());
            }
        }

        // Use system default editor if custom failed or not configured
        if (!opened) {
            QUrl fileUrl = QUrl::fromLocalFile(targetFilePath);
            opened = QDesktopServices::openUrl(fileUrl);

            if (!opened) {
                QString errorText = customAttempted
                    ? QString("Failed to open file with custom editor (%1) or system default.").arg(customEditorPath)
                    : QString("Failed to open file with system default editor.");
                QtDialogs::showError(this, "Error", errorText);

                // Clean up the temp file if opening failed
                if (usedTemporaryFile) {
                    QFile::remove(targetFilePath);
                }
            } else {
                spdlog::info("MainWindow: Successfully opened file with system default editor: {}", targetFilePath.toStdString());
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
                if (foundMiscSection && currentSection == "[Misc]" && (line.starts_with("StartingMap=") || line.starts_with(";StartingMap="))) {
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

void MainWindow::launchGame(const std::filesystem::path& executablePath) {
    spdlog::info("Launching game executable: {}", executablePath.string());

    // Create and configure the game process
    QProcess* gameProcess = new QProcess(this);

    // Set working directory to the executable's directory
    gameProcess->setWorkingDirectory(QString::fromStdString(executablePath.parent_path().string()));

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

    // Launch the executable (use 'open' on macOS for .app bundles)
    if (executablePath.extension() == ".app") {
        gameProcess->start("open", QStringList() << QString::fromStdString(executablePath.string()));
    } else {
        gameProcess->start(QString::fromStdString(executablePath.string()));
    }

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
                std::string proFileName = ProHelper::basePath(*_resourcesShared, mapObject.pro_pid);
                spdlog::debug("MainWindow::openProEditorForSelectedObject() - opening PRO: {}", proFileName);

                auto fileData = _resourcesShared->files().readRawBytes(proFileName);
                if (!fileData) {
                    spdlog::error("MainWindow::openProEditorForSelectedObject() - could not open PRO file: {}", proFileName);
                    return false;
                }

                // Create temporary file
                auto pro = ReaderFactory::readFileFromMemory<Pro>(*fileData, proFileName);
                if (!pro) {
                    spdlog::error("MainWindow::openProEditorForSelectedObject() - could not parse PRO file");
                    return false;
                }

                // Create and show PRO editor dialog
                ProEditorDialog dialog(*_resourcesShared, std::shared_ptr<Pro>(pro.release()), this);
                dialog.exec();

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
