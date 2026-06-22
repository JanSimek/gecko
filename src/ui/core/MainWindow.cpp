#define QT_NO_EMIT
#include "MainWindow.h"
#include "EditorWidget.h"
#include "ui/widgets/LoadingWidget.h"
#include "ui/widgets/WelcomeWidget.h"
#include "ui/widgets/SFMLWidget.h"
#include "ui/panels/SelectionPanel.h"
#include "ui/panels/MapInfoPanel.h"
#include "ui/panels/TilePalettePanel.h"
#include "ui/panels/ObjectPalettePanel.h"
#include "ui/panels/FileBrowserPanel.h"
#ifdef GECK_SCRIPTING_ENABLED
#include "ui/panels/ScriptConsoleWidget.h"
#include "scripting/LuaScriptRuntime.h" // ScriptResult
#endif
#include "ui/tiles/TilePlacementManager.h"
#include "ui/tools/ExitGridPlacementManager.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/MapBrowserDialog.h"
#include "ui/dialogs/PatternBrowserDialog.h"
#include "ui/UIConstants.h"
#include "resource/GameResources.h"
#include "state/loader/MapLoader.h"
#include "state/loader/DataPathLoader.h"
#include "state/GameLauncher.h"
#include "selection/SelectionState.h"
#include "selection/SelectionManager.h"
#include "pattern/PatternBuilder.h"
#include "pattern/PatternLibrary.h"
#include "pattern/PatternSerializer.h"
#include "format/map/Map.h"
#include "util/Types.h"
#include "ui/Settings.h"
#include "resource/WritableDataRoot.h"
#include "ui/QtDialogs.h"
#include "ui/ExternalEditorLauncher.h"
#include "reader/lst/LstReader.h"
#include "format/lst/Lst.h"
#include "format/map/MapObject.h"
#include "editor/Object.h"
#include "ui/IconHelper.h"

#include <functional>

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QLabel>
#include <QCloseEvent>
#include <QMessageBox>
#include <QKeyEvent>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QIcon>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>

namespace geck {

MainWindow::MainWindow(std::shared_ptr<resource::GameResources> resources, std::shared_ptr<Settings> settings, QWidget* parent)
    : QMainWindow(parent)
    , _centralStack(nullptr)
    , _gameLoopTimer(new QTimer(this))
    , _resourcesShared(std::move(resources))
    , _settings(std::move(settings))
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

    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    setupUI();

    // The unique_ptr owns the launcher, so it has no QObject parent (it still
    // parents its own QProcess children). `this` is only the dialog parent.
    _gameLauncher = std::make_unique<GameLauncher>(*_resourcesShared, _settings, this, [this](const QString& message) { showStatusMessage(message); }, nullptr);

    _externalEditorLauncher = std::make_unique<ExternalEditorLauncher>(*_resourcesShared, _settings, this);

    restoreDockWidgetState();

    // Capture the restored visibility, then hide panels until a map is loaded
    snapshotPanelVisibility();
    hidePanelsForNoMap();

    // Reflect actual visibility once restoration settles
    QTimer::singleShot(200, this, &MainWindow::updatePanelMenuActions);

    connect(_gameLoopTimer, &QTimer::timeout, this, &MainWindow::updateAndRender);
}

MainWindow::~MainWindow() {
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

    _currentEditorWidget->setMainWindow(this);
    _currentEditorWidget->init();

    connectToEditorWidget();
    connect(_currentEditorWidget, &EditorWidget::undoStackChanged, this, &MainWindow::updateUndoRedoActions);
    // Any edit (or undo/redo) marks the map dirty for the close/title prompts.
    connect(_currentEditorWidget, &EditorWidget::undoStackChanged, this, [this]() { setMapModified(true); });
    // Non-undoable Console-script mutations (newMap/setPlayerStart) flag the map dirty too.
    connect(_currentEditorWidget, &EditorWidget::mapModifiedByScript, this, [this]() { setMapModified(true); });

    syncMenuStateToEditorWidget();

    if (_currentEditorWidget->getMap()) {
        updateMapInfo(_currentEditorWidget->getMap());
        restorePanelVisibilitySnapshot();
    } else {
        snapshotPanelVisibility();
        hidePanelsForNoMap();
    }

    // A freshly loaded/created map starts clean; show its name in the title bar.
    _mapModified = false;
    updateWindowTitle();

    QTimer::singleShot(50, this, &MainWindow::updatePanelMenuActions);
    updateUndoRedoActions();
}

void MainWindow::setupUI() {
    _centralStack = new QStackedWidget(this);
    setCentralWidget(_centralStack);

    // Welcome widget is shown when no map is loaded
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
    // MainWindow menu signals - these work regardless of EditorWidget state
    connect(this, &MainWindow::newMapRequested, [this]() {
        if (!maybeSaveChanges()) {
            return; // user cancelled — keep the current map
        }
        // Building the editor loads essential art (the hex-grid overlay, etc.); if Fallout 2 data
        // isn't configured those files are missing and the load throws. Surface that as a "Missing
        // Game Files" dialog — the same way a failed map load does — instead of letting the
        // exception abort the app.
        try {
            if (_currentEditorWidget) {
                _currentEditorWidget->createNewMap();
            } else {
                // Create an EditorWidget with an empty map, then populate it
                auto editorWidget = std::make_unique<EditorWidget>(*_resourcesShared, nullptr);
                setEditorWidget(std::move(editorWidget));
                _currentEditorWidget->createNewMap();
            }
            // The new map is clean; refresh the title to its name.
            _mapModified = false;
            updateWindowTitle();
        } catch (const std::exception& e) {
            spdlog::error("Failed to create a new map: {}", e.what());
            QtDialogs::showError(this, "Missing Game Files",
                QString("Could not create a new map:\n%1\n\nConfigure the Fallout 2 data files "
                        "(master.dat) in Preferences and try again.")
                    .arg(e.what()));
        }
    });
    connect(this, &MainWindow::openMapRequested, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->openMap();
        } else {
            // No editor yet: handle the open directly
            QString mapPath = QtDialogs::openMapFile(this, "Choose Fallout 2 map to load");
            if (!mapPath.isEmpty()) {
                handleMapLoadRequest(mapPath.toStdString(), true); // Force filesystem for File menu
            }
        }
    });
    connect(this, &MainWindow::saveMapRequested, [this]() {
        if (_currentEditorWidget && _currentEditorWidget->saveMap(writableMapsDir())) {
            handleMapSaved();
        }
    });
    connect(this, &MainWindow::saveMapAsRequested, [this]() {
        if (_currentEditorWidget && _currentEditorWidget->saveMapAs(writableMapsDir())) {
            handleMapSaved();
        }
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
    addMenuAction(_fileMenu, ":/icons/actions/open.svg", "&Browse Maps...", &MainWindow::showMapBrowserDialog, QKeySequence("Ctrl+B"), "Browse available maps as thumbnails");
    addMenuAction(_fileMenu, ":/icons/actions/save.svg", "&Save Map", &MainWindow::saveMapRequested, QKeySequence::Save, "Save current map");
    addMenuAction(_fileMenu, ":/icons/actions/save.svg", "Save Map &As...", &MainWindow::saveMapAsRequested, QKeySequence::SaveAs, "Save the current map to a chosen file");

    _fileMenu->addSeparator();
    addMenuAction(_fileMenu, ":/icons/actions/settings.svg", "&Preferences...", &MainWindow::showPreferences, QKeySequence::Preferences, "Open application preferences");

    _fileMenu->addSeparator();
    addMenuAction(_fileMenu, ":/icons/actions/quit.svg", "&Quit", &QWidget::close, QKeySequence::Quit, "Exit the application");

    _editMenu = _menuBar->addMenu("&Edit");
    addMenuAction(_editMenu, ":/icons/actions/select-all.svg", "Select &All", &MainWindow::selectAllRequested, QKeySequence("Ctrl+A"), "Select all items of current type");
    addMenuAction(_editMenu, ":/icons/actions/deselect.svg", "&Deselect All", &MainWindow::deselectAllRequested, QKeySequence("Ctrl+D"), "Clear all selections");

    _editMenu->addSeparator();
    addMenuAction(_editMenu, ":/icons/actions/scroll-blocker.svg", "Scroll &Blocker Rectangle", &MainWindow::toggleScrollBlockerRectangleMode, QKeySequence("B"), "Draw rectangle and place scroll blockers on borders");
    addMenuAction(_editMenu, ":/icons/actions/save.svg", "Save Selection as &Pattern...", &MainWindow::showSavePatternDialog, QKeySequence(), "Save the current selection as a reusable prefab pattern");
    addMenuAction(_editMenu, ":/icons/actions/open.svg", "S&tamp Pattern...", &MainWindow::showStampPatternDialog, QKeySequence(), "Load a prefab pattern and click to place it");

    _editMenu->addSeparator();

    _undoAction = _editMenu->addAction("&Undo");
    _undoAction->setShortcut(QKeySequence::Undo);
    _undoAction->setStatusTip("Undo last edit");
    connect(_undoAction, &QAction::triggered, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->undoLastEdit();
            updateUndoRedoActions();
            if (_selectionPanel)
                _selectionPanel->refresh();
        }
    });

    _redoAction = _editMenu->addAction("&Redo");
    _redoAction->setShortcut(QKeySequence::Redo);
    _redoAction->setStatusTip("Redo last edit");
    connect(_redoAction, &QAction::triggered, [this]() {
        if (_currentEditorWidget) {
            _currentEditorWidget->redoLastEdit();
            updateUndoRedoActions();
            if (_selectionPanel)
                _selectionPanel->refresh();
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

    _mergeOutlinesAction = _viewMenu->addAction("&Merge Adjacent Selection Outlines");
    _mergeOutlinesAction->setCheckable(true);
    _mergeOutlinesAction->setChecked(_settings ? _settings->getMergeSelectionOutlines() : true);
    _mergeOutlinesAction->setStatusTip("Merge touching selected objects of the same type into a single outline");
    connect(_mergeOutlinesAction, &QAction::toggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setMergeSelectionOutlines(enabled);
        if (_settings) {
            _settings->setMergeSelectionOutlines(enabled);
            _settings->save();
        }
    });

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

void MainWindow::applySelectionLayersFromMenu() {
    if (!_currentEditorWidget || !_floorLayerAction) {
        return;
    }

    SelectionLayers layers;
    layers.floorTiles = _floorLayerAction->isChecked();
    layers.roofTiles = _roofLayerAction->isChecked();
    layers.objects = _objectsLayerAction->isChecked();

    // A layer combination is exclusive with the special tools below the separator.
    for (QAction* menuAction : _selectionModeMenu->actions()) {
        if (menuAction->data().isValid()) {
            menuAction->setChecked(false);
        }
    }

    _currentEditorWidget->setActiveSelectionLayers(layers);

    QStringList parts;
    if (layers.floorTiles) {
        parts << "Floor";
    }
    if (layers.roofTiles) {
        parts << "Roof";
    }
    if (layers.objects) {
        parts << "Objects";
    }
    _selectionModeAction->setText(layers.all() ? "All" : (parts.isEmpty() ? "None" : parts.join(" + ")));
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
        { ":/icons/actions/open.svg", "Browse Maps", "Browse available maps as thumbnails", {}, [this]() { showMapBrowserDialog(); } },
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

    _mainToolBar->addSeparator();

    // Selection mode action with dropdown menu. The three layers (floor / roof / objects) are
    // combinable checkboxes; the tools below them are exclusive single modes.
    _selectionModeAction = _mainToolBar->addAction(createIcon(":/icons/actions/select.svg"), "All");
    _selectionModeAction->setToolTip("Choose which layers to select (combine floor / roof / objects)");

    _selectionModeMenu = new QMenu(this);

    // Combinable layer checkboxes (non-exclusive). Default: all on, i.e. classic "All".
    auto addLayerAction = [this](const QString& label) {
        QAction* action = _selectionModeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this](bool) { applySelectionLayersFromMenu(); });
        return action;
    };
    _floorLayerAction = addLayerAction("Floor Tiles");
    _roofLayerAction = addLayerAction("Roof Tiles");
    _objectsLayerAction = addLayerAction("Objects");

    _selectionModeMenu->addSeparator();

    // Exclusive special tools — picking one clears the layer checkboxes (and the other tools).
    for (SelectionMode mode : { SelectionMode::ROOF_TILES_ALL, SelectionMode::HEXES, SelectionMode::SCROLL_BLOCKER_RECTANGLE }) {
        QAction* action = _selectionModeMenu->addAction(selectionModeToString(mode));
        action->setData(static_cast<int>(mode));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, mode]() {
            if (!_currentEditorWidget) {
                return;
            }
            _currentEditorWidget->setSelectionMode(mode);
            for (QAction* layer : { _floorLayerAction, _roofLayerAction, _objectsLayerAction }) {
                const QSignalBlocker block(layer); // don't re-run applySelectionLayersFromMenu
                layer->setChecked(false);
            }
            for (QAction* menuAction : _selectionModeMenu->actions()) {
                if (menuAction->data().isValid()) {
                    menuAction->setChecked(menuAction->data().toInt() == static_cast<int>(mode));
                }
            }
            _selectionModeAction->setText(selectionModeToString(mode));
        });
    }

    connect(_selectionModeAction, &QAction::triggered, this, [this]() {
        QWidget* actionWidget = _mainToolBar->widgetForAction(_selectionModeAction);
        if (actionWidget) {
            // Position the dropdown directly below the toolbar button
            QPoint menuPos = actionWidget->mapToGlobal(QPoint(0, actionWidget->height()));
            _selectionModeMenu->exec(menuPos);
        }
    });

    _mainToolBar->addSeparator();

    // Stored so it can be disabled while stamping a pattern — otherwise its "R" shortcut
    // swallows the key before it reaches the viewport, where R cycles pattern variants.
    _rotateAction = addToolAction(":/icons/actions/rotate.svg", "Rotate", &MainWindow::rotateObjectRequested, "Rotate selected object", QKeySequence("R"));

    _mainToolBar->addSeparator();

    setupToolModeActions();

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

void MainWindow::setupToolModeActions() {
    // Tool-mode group (mutually exclusive; kept in sync with the active EditorMode).
    _selectToolAction = _mainToolBar->addAction(createIcon(":/icons/actions/select.svg"), "Select");
    _selectToolAction->setStatusTip("Select and move objects (exits any active tool)");
    _selectToolAction->setCheckable(true);
    _selectToolAction->setChecked(true); // Select is the default mode
    connect(_selectToolAction, &QAction::triggered, this, [this](bool) {
        if (_currentEditorWidget) {
            _currentEditorWidget->setMode(EditorMode::Select);
        }
    });

    _markExitsAction = _mainToolBar->addAction(createIcon(":/icons/actions/door-exit.svg"), "Mark Exits");
    _markExitsAction->setStatusTip("Select exit grids to edit their properties");
    _markExitsAction->setCheckable(true);
    connect(_markExitsAction, &QAction::triggered, this, [this](bool checked) {
        if (_currentEditorWidget) {
            _currentEditorWidget->setMarkExitsMode(checked);
        }
    });

    _placeExitGridAction = _mainToolBar->addAction(createIcon(":/icons/actions/door-exit.svg"), "Place Exit Grids");
    _placeExitGridAction->setStatusTip("Place new exit-grid markers by clicking individual hexes");
    _placeExitGridAction->setCheckable(true);
    connect(_placeExitGridAction, &QAction::triggered, this, [this](bool checked) {
        if (_currentEditorWidget) {
            _currentEditorWidget->setExitGridPlacementMode(checked);
        }
    });
}

void MainWindow::syncToolModeActions(EditorMode mode) {
    const auto sync = [mode](QAction* action, EditorMode actionMode) {
        if (action) {
            QSignalBlocker blocker(action);
            action->setChecked(mode == actionMode);
        }
    };
    sync(_selectToolAction, EditorMode::Select);
    sync(_markExitsAction, EditorMode::MarkExits);
    sync(_placeExitGridAction, EditorMode::PlaceExitGrid);

    // Free up "R" for variant cycling while a pattern is being stamped.
    if (_rotateAction) {
        _rotateAction->setEnabled(mode != EditorMode::StampPattern);
    }
}

#ifdef GECK_SCRIPTING_ENABLED
void MainWindow::wireScriptConsole() {
    connect(_scriptConsole, &ScriptConsoleWidget::runRequested, this, [this](const QString& source) {
        if (!_currentEditorWidget) {
            _scriptConsole->showResult(false, QString(), tr("Open a map before running a script."));
            return;
        }
        const ScriptResult result = _currentEditorWidget->runScript(source.toStdString());
        _scriptConsole->showResult(result.ok, QString::fromStdString(result.output), QString::fromStdString(result.error));
    });

    if (_viewMenu) {
        _viewMenu->addSeparator();
        QAction* consoleAction = _scriptConsoleDock->toggleViewAction();
        consoleAction->setText(tr("Script &Console"));
        _viewMenu->addAction(consoleAction);
    }
}
#endif

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

    _mapInfoPanel = new MapInfoPanel(*_resourcesShared, _settings);
    _mapInfoDock = createDock("Map Information", "MapInfoDock", _mapInfoPanel, Qt::RightDockWidgetArea, QSizePolicy::Preferred, ui::constants::dock::MIN_HEIGHT_SMALL);

    _selectionPanel = new SelectionPanel(*_resourcesShared);
    _selectionDock = createDock("Selection", "SelectionDock", _selectionPanel, Qt::RightDockWidgetArea, QSizePolicy::Preferred, ui::constants::dock::MIN_HEIGHT_SMALL);

    _tilePalettePanel = new TilePalettePanel(*_resourcesShared);
    _tilePaletteDock = createDock("Tile Palette", "TilePaletteDock", _tilePalettePanel, Qt::LeftDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_LARGE);

    _objectPalettePanel = new ObjectPalettePanel(*_resourcesShared);
    _objectPaletteDock = createDock("Object Palette", "ObjectPaletteDock", _objectPalettePanel, Qt::LeftDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_LARGE);

    _fileBrowserPanel = new FileBrowserPanel(_resourcesShared, _settings);
    _fileBrowserDock = createDock("Virtual File System Browser", "FileBrowserDock", _fileBrowserPanel, Qt::LeftDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_LARGE);
    connectFileBrowserSignals();
    connectPanelSignals();

#ifdef GECK_SCRIPTING_ENABLED
    // Script console (Luau): a separate bottom dock, hidden until opened from the View menu. It is
    // not one of the managed palette docks, so it stays out of the layout-persistence machinery.
    _scriptConsole = new ScriptConsoleWidget();
    _scriptConsoleDock = createDock("Script Console", "ScriptConsoleDock", _scriptConsole, Qt::BottomDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_SMALL);
    _scriptConsoleDock->hide();
    wireScriptConsole();
#endif

    // MainWindow signals → current editor widget (connected once; both sender
    // and receiver are MainWindow, so these survive for the lifetime of the window)
    connect(this, &MainWindow::showObjectsToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowObjects(enabled);
    });
    connect(this, &MainWindow::showCrittersToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowCritters(enabled);
    });
    connect(this, &MainWindow::showWallsToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowWalls(enabled);
    });
    connect(this, &MainWindow::showRoofsToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowRoof(enabled);
    });
    connect(this, &MainWindow::showScrollBlockersToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowScrollBlk(enabled);
    });
    connect(this, &MainWindow::showWallBlockersToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowWallBlockers(enabled);
    });
    connect(this, &MainWindow::showHexGridToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowHexGrid(enabled);
    });
    connect(this, &MainWindow::showLightOverlaysToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowLightOverlays(enabled);
    });
    connect(this, &MainWindow::showExitGridsToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowExitGrids(enabled);
    });
    connect(this, &MainWindow::elevationChanged, this, [this](int elevation) {
        if (_currentEditorWidget)
            _currentEditorWidget->changeElevation(elevation);
    });
    connect(this, &MainWindow::rotateObjectRequested, this, [this]() {
        if (_currentEditorWidget)
            _currentEditorWidget->rotateSelectedObject();
    });
    connect(this, &MainWindow::toggleScrollBlockerRectangleMode, this, [this]() {
        if (_currentEditorWidget)
            _currentEditorWidget->toggleScrollBlockerRectangleMode();
    });

    applyDefaultPanelDockLayout();

    // Let Qt handle initial dock sizing; explicit resizeDocks() calls would interfere with user resizes
    spdlog::debug("Dock widget setup completed with flexible sizing and state monitoring");

    // Qt already renders dock widgets above the central SFML widget, avoiding redraw issues
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
        } else if (ExternalEditorLauncher::isTextFile(filePath)) {
            spdlog::info("MainWindow: Opening text file from file browser: {}", filePath.toStdString());
            _externalEditorLauncher->openFile(filePath);
        } else {
            spdlog::debug("MainWindow: Unsupported file type double-clicked: {}", filePath.toStdString());
            QtDialogs::showInfo(this, "File Type Not Supported",
                QString("File type not supported for opening: %1\n\nYou can export the file using the right-click context menu.").arg(filePath));
        }
    });

#ifdef GECK_SCRIPTING_ENABLED
    // "Execute script" in the file browser loads the .luau source into the Script Console and
    // reveals it, ready to Run. (The console dock only exists in a scripting-enabled build.)
    connect(_fileBrowserPanel, &FileBrowserPanel::executeScriptRequested, this, [this](const QString& source) {
        if (!_scriptConsole || !_scriptConsoleDock) {
            return;
        }
        _scriptConsole->setSource(source);
        _scriptConsoleDock->show();
        _scriptConsoleDock->raise();
    });
#endif
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
    _mapInfoPanel = new MapInfoPanel(*_resourcesShared, _settings);
    replaceDockPanelWidget(_mapInfoDock, _mapInfoPanel, QSizePolicy::Preferred);

    _selectionPanel = new SelectionPanel(*_resourcesShared);
    replaceDockPanelWidget(_selectionDock, _selectionPanel, QSizePolicy::Preferred);

    _tilePalettePanel = new TilePalettePanel(*_resourcesShared);
    replaceDockPanelWidget(_tilePaletteDock, _tilePalettePanel, QSizePolicy::Expanding);

    _objectPalettePanel = new ObjectPalettePanel(*_resourcesShared);
    replaceDockPanelWidget(_objectPaletteDock, _objectPalettePanel, QSizePolicy::Expanding);

    _fileBrowserPanel = new FileBrowserPanel(_resourcesShared, _settings);
    replaceDockPanelWidget(_fileBrowserDock, _fileBrowserPanel, QSizePolicy::Expanding);
    connectFileBrowserSignals();
    connectPanelSignals();

    // Re-wire per-widget connections since the old panels were destroyed
    if (_currentEditorWidget) {
        connectToEditorWidget();
    }
}

void MainWindow::rebuildGameResourcesFromSettings() {
    // Rebuilding resources closes the current map, so honour the same unsaved-changes prompt the
    // New/Open/Quit paths use. Cancelling keeps the map (the data-path change just won't take effect
    // until the next rebuild).
    if (hasActiveMap() && !maybeSaveChanges()) {
        return;
    }

    const auto dataPaths = _settings->getDataPaths();

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

void MainWindow::updateWindowTitle() {
    const QString base = QStringLiteral("Gecko - Fallout 2 Map Editor");
    const Map* map = _currentEditorWidget ? _currentEditorWidget->getMap() : nullptr;
    if (map && !map->filename().empty()) {
        // Use a literal "*" so the modified marker is visible on every platform (Qt's "[*]" +
        // setWindowModified shows nothing in the title text on macOS — only a close-button dot).
        const QString modifiedMark = _mapModified ? QStringLiteral("*") : QString();
        setWindowTitle(QStringLiteral("%1%2 - %3").arg(QString::fromStdString(map->filename()), modifiedMark, base));
    } else {
        setWindowTitle(base);
    }
    setWindowModified(_mapModified); // also drive the native indicator (e.g. macOS close-button dot)
}

void MainWindow::setMapModified(bool modified) {
    if (_mapModified == modified) {
        return;
    }
    _mapModified = modified;
    updateWindowTitle(); // refresh the "*" in the title (and the native marker)
}

std::filesystem::path MainWindow::writableMapsDir() const {
    if (!_settings) {
        return {};
    }
    const auto root = resource::findWritableDataPath(_settings->getDataPaths());
    if (!root) {
        return {}; // no writable Data Path -> the save dialog falls back to its last-used directory
    }
    const std::filesystem::path mapsDir = *root / "maps";
    std::error_code ec;
    std::filesystem::create_directories(mapsDir, ec); // so the save dialog can default into it
    if (!std::filesystem::is_directory(mapsDir, ec)) {
        return {}; // couldn't create/use maps/ (permissions, or it's a file) -> fall back to last-used dir
    }
    return mapsDir;
}

void MainWindow::handleMapSaved() {
    if (_mapInfoPanel) {
        _mapInfoPanel->persistMapNames(); // flush any edited Map name / Lookup name alongside the .map
    }
    _mapModified = false;
    updateWindowTitle(); // clear the "[*]" and reflect any Save As rename
}

bool MainWindow::maybeSaveChanges() {
    if (!_mapModified || !_currentEditorWidget || !_currentEditorWidget->getMap()) {
        return true; // nothing unsaved to lose
    }
    const QString name = QString::fromStdString(_currentEditorWidget->getMap()->filename());
    const QMessageBox::StandardButton choice = QMessageBox::warning(this, tr("Unsaved Changes"),
        tr("\"%1\" has unsaved changes.\n\nSave them before continuing?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (choice == QMessageBox::Save) {
        if (_currentEditorWidget->saveMap(writableMapsDir())) {
            handleMapSaved();
            return true;
        }
        return false; // save cancelled or failed — abort rather than discard
    }
    return choice == QMessageBox::Discard; // Discard proceeds; Cancel (or closed) aborts
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSaveChanges()) {
        event->ignore(); // user cancelled the quit to keep their unsaved map
        return;
    }
    _suppressPanelPreferenceUpdates = true;
    _suppressPanelSnapshotUpdates = true;
    stopGameLoop();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Handle special shortcuts before forwarding to SFML
    if (event->key() == Qt::Key_P && _currentEditorWidget) {
        if (_selectionPanel ? _selectionPanel->openProEditorForSelectedObject() : false) {
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

    if (pressed) {
        sfmlEvent = sf::Event::KeyPressed{ key };
    } else {
        sfmlEvent = sf::Event::KeyReleased{ key };
    }

    // SFML 3 events don't carry modifier state; check modifiers via sf::Keyboard::isKeyPressed() when needed
}

void MainWindow::connectPanelSignals() {
    // Connections from panel signals to lambdas that resolve _currentEditorWidget
    // at call time. Called from setupDockWidgets() and rebuildResourcePanels()
    // (panel recreation destroys old connections automatically).

    // SelectionPanel signals → MainWindow / current editor widget
    if (_selectionPanel) {
        connect(_selectionPanel, &SelectionPanel::objectFrmChanged,
            this, [this](std::shared_ptr<Object> object, uint32_t newFrmPid) {
                if (_currentEditorWidget)
                    _currentEditorWidget->onObjectFrmChanged(object, newFrmPid);
            });
        connect(_selectionPanel, &SelectionPanel::objectFrmPathChanged,
            this, [this](std::shared_ptr<Object> object, const std::string& newFrmPath) {
                if (_currentEditorWidget)
                    _currentEditorWidget->onObjectFrmPathChanged(object, newFrmPath);
            });
        connect(_selectionPanel, &SelectionPanel::statusMessage, this, &MainWindow::showStatusMessage);
        connect(_selectionPanel, &SelectionPanel::requestExitGridEditor,
            this, [this](std::shared_ptr<Object> object) {
                if (!_currentEditorWidget || !object || !object->hasMapObject())
                    return;
                auto* exitGridManager = _currentEditorWidget->getExitGridPlacementManager();
                if (exitGridManager) {
                    auto mapObjectPtr = object->getMapObjectPtr();
                    if (mapObjectPtr && mapObjectPtr->isExitGridMarker()) {
                        exitGridManager->editExitGridProperties(mapObjectPtr);
                    }
                }
            });
        connect(_selectionPanel, &SelectionPanel::requestInstanceEdit,
            this, [this](std::shared_ptr<Object> object, MapObjectInstanceState before, MapObjectInstanceState after, QString description) {
                if (!_currentEditorWidget || !object || !object->hasMapObject())
                    return;
                _currentEditorWidget->registerInstanceEdit(object->getMapObjectPtr(),
                    before, after, description.toStdString());
            });
        connect(_selectionPanel, &SelectionPanel::requestInventoryEdit,
            this, [this](std::shared_ptr<MapObject> container, std::vector<std::shared_ptr<MapObject>> before, std::vector<std::shared_ptr<MapObject>> after) {
                if (_currentEditorWidget && container)
                    _currentEditorWidget->registerInventoryEdit(container, std::move(before), std::move(after));
            });
        connect(_selectionPanel, &SelectionPanel::requestAttachScript,
            this, [this](std::shared_ptr<MapObject> object, int scriptType, uint32_t programIndex) {
                if (_currentEditorWidget && object)
                    _currentEditorWidget->attachScript(object, scriptType, programIndex);
            });
        connect(_selectionPanel, &SelectionPanel::requestDetachScript,
            this, [this](std::shared_ptr<MapObject> object) {
                if (_currentEditorWidget && object)
                    _currentEditorWidget->detachScript(object);
            });
        connect(_selectionPanel, &SelectionPanel::requestObjectHighlight,
            this, [this](std::shared_ptr<Object> object) {
                if (!_currentEditorWidget || !object)
                    return;
                auto* selectionManager = _currentEditorWidget->getSelectionManager();
                auto& selectionState = selectionManager->getMutableSelection();

                selection::SelectedItem item;
                item.type = selection::SelectionType::OBJECT;
                item.data = object;
                if (!selectionState.hasItem(item)) {
                    selectionState.addItem(item);
                }

                _currentEditorWidget->selectionChanged(selectionState, _currentEditorWidget->getCurrentElevation());
                if (_currentEditorWidget->getSFMLWidget()) {
                    _currentEditorWidget->getSFMLWidget()->update();
                    _currentEditorWidget->getSFMLWidget()->repaint();
                }
            });
    }

    // TilePalettePanel signals → current editor widget
    if (_tilePalettePanel) {
        connect(_tilePalettePanel, &TilePalettePanel::tileSelected,
            this, [this](int tileIndex, bool isRoof) {
                if (!_currentEditorWidget)
                    return;
                if (tileIndex >= 0) {
                    _currentEditorWidget->setTilePlacementMode(true, tileIndex, isRoof);
                    updateModeDisplay("Mode: Tile painting", ":/icons/actions/paint.svg");
                } else {
                    _currentEditorWidget->setTilePlacementMode(false);
                    updateModeDisplay("Mode: All", ":/icons/actions/select.svg");
                }
            });
        connect(_tilePalettePanel, &TilePalettePanel::placementModeChanged,
            this, [this]([[maybe_unused]] TilePalettePanel::PlacementMode mode) {
                if (!_currentEditorWidget)
                    return;
                _currentEditorWidget->setTilePlacementAreaFill(true);
                _currentEditorWidget->setTilePlacementReplaceMode(false);
                spdlog::debug("Set unified tile placement mode");
            });
        connect(_tilePalettePanel, &TilePalettePanel::replaceSelectedTiles,
            this, [this](int newTileIndex) {
                if (_currentEditorWidget)
                    _currentEditorWidget->replaceSelectedTiles(newTileIndex);
            });
    }

    // MapInfoPanel signals → current editor widget
    if (_mapInfoPanel) {
        connect(_mapInfoPanel, &MapInfoPanel::selectPlayerPositionRequested,
            this, [this]() {
                if (_currentEditorWidget)
                    _currentEditorWidget->enterPlayerPositionSelectionMode();
            });
        connect(_mapInfoPanel, &MapInfoPanel::centerViewOnPlayerPositionRequested,
            this, [this]() {
                if (_currentEditorWidget)
                    _currentEditorWidget->centerViewOnPlayerPosition();
            });
        connect(_mapInfoPanel, &MapInfoPanel::elevationAdded,
            this, [this](int elevation) {
                if (!_currentEditorWidget)
                    return;
                setMapModified(true);
                updateElevationMenu(_currentEditorWidget->getMap());
                if (_currentEditorWidget->getCurrentElevation() == elevation) {
                    _currentEditorWidget->loadTileSprites();
                    spdlog::info("MainWindow: Reloaded sprites for newly added elevation {}", elevation);
                }
            });
        connect(_mapInfoPanel, &MapInfoPanel::elevationRemoved,
            this, [this](int elevation) {
                if (!_currentEditorWidget)
                    return;
                setMapModified(true);
                updateElevationMenu(_currentEditorWidget->getMap());
                if (_currentEditorWidget->getCurrentElevation() == elevation) {
                    auto* map = _currentEditorWidget->getMap();
                    if (map) {
                        uint32_t flags = map->getMapFile().header.flags;
                        if ((flags & 0x2) == 0)
                            elevationChanged(ELEVATION_1);
                        else if ((flags & 0x4) == 0)
                            elevationChanged(ELEVATION_2);
                        else if ((flags & 0x8) == 0)
                            elevationChanged(ELEVATION_3);
                    }
                    spdlog::info("MainWindow: Switched away from removed elevation {}", elevation);
                }
            });
        connect(_mapInfoPanel, &MapInfoPanel::clearElevationRequested,
            this, [this](int elevation) {
                if (_currentEditorWidget)
                    _currentEditorWidget->clearElevationObjects(elevation);
            });
        connect(_mapInfoPanel, &MapInfoPanel::copyElevationRequested,
            this, [this](int from, int to) {
                if (_currentEditorWidget)
                    _currentEditorWidget->copyElevation(from, to);
            });
        connect(_mapInfoPanel, &MapInfoPanel::addSpatialScriptRequested,
            this, [this](int programIndex, int tile, int elevation, int radius) {
                if (_currentEditorWidget)
                    _currentEditorWidget->addSpatialScript(static_cast<uint32_t>(programIndex), tile, elevation, radius);
            });

        // Header edits in the Info panel write straight to the map (no undo command), so flag the map
        // modified here. These also fire while the panel is being populated from a freshly loaded map,
        // but setEditorWidget()/createNewMap clear the flag right after populating, so that's harmless.
        connect(_mapInfoPanel, &MapInfoPanel::playerPositionChanged, this, [this](int) { setMapModified(true); });
        connect(_mapInfoPanel, &MapInfoPanel::playerElevationChanged, this, [this](int) { setMapModified(true); });
        connect(_mapInfoPanel, &MapInfoPanel::playerOrientationChanged, this, [this](int) { setMapModified(true); });
        connect(_mapInfoPanel, &MapInfoPanel::mapScriptIdChanged, this, [this](int) { setMapModified(true); });
        connect(_mapInfoPanel, &MapInfoPanel::darknessChanged, this, [this](int) { setMapModified(true); });
        connect(_mapInfoPanel, &MapInfoPanel::timestampChanged, this, [this](int) { setMapModified(true); });
        // Editing a Map name / Lookup name marks the map modified; the value is written to the writable
        // copy when the map is saved (see the saveMap handlers, which call persistMapNames()).
        connect(_mapInfoPanel, &MapInfoPanel::mapNamesChanged, this, [this]() { setMapModified(true); });
    }
}

void MainWindow::connectToEditorWidget() {
    if (!_currentEditorWidget) {
        return;
    }

    // Per-widget connections (sender is _currentEditorWidget) are auto-cleaned when
    // the old EditorWidget is destroyed.
    if (_selectionPanel) {
        _selectionPanel->setMap(_currentEditorWidget->getMap());
        connect(_currentEditorWidget, &EditorWidget::selectionChanged,
            _selectionPanel, &SelectionPanel::handleSelectionChanged);
    }

    if (_tilePalettePanel) {
        _tilePalettePanel->setMap(_currentEditorWidget->getMap());
        _tilePalettePanel->setSelectionManager(_currentEditorWidget->getSelectionManager());
    }

    connect(_currentEditorWidget, &EditorWidget::statusMessageRequested, this, &MainWindow::showStatusMessage);
    connect(_currentEditorWidget, &EditorWidget::statusMessageClearRequested, this, &MainWindow::clearStatusMessage);
    // Keep the checkable tool-mode toolbar actions in sync with the active mode
    // (entering any mode exits the others via EditorWidget::setMode).
    connect(_currentEditorWidget, &EditorWidget::editorModeChanged, this, [this](EditorMode mode) {
        syncToolModeActions(mode);
    });
    // The editor widget doesn't emit editorModeChanged on connect, so seed the
    // toolbar state from its current mode (e.g. after switching maps).
    syncToolModeActions(_currentEditorWidget->currentMode());
    connect(_currentEditorWidget, &EditorWidget::hexHoverChanged, this, &MainWindow::updateHexIndexDisplay);
    connect(_currentEditorWidget, &EditorWidget::mapLoadRequested, this, [this](const std::string& mapPath) {
        handleMapLoadRequest(mapPath, true);
    });

    if (_mapInfoPanel) {
        connect(_currentEditorWidget, &EditorWidget::playerPositionSelected,
            this, [this](int hexPosition) {
                if (_mapInfoPanel && _currentEditorWidget) {
                    _mapInfoPanel->setPlayerPosition(hexPosition, _currentEditorWidget->getCurrentElevation());
                }
            });
    }

    applySelectionColorsToEditor();

    spdlog::info("Connected EditorWidget instance signals");
}

void MainWindow::applySelectionColorsToEditor() {
    if (!_currentEditorWidget || !_settings) {
        return;
    }

    const auto toQ = [](const sf::Color& c) { return QColor(c.r, c.g, c.b); };
    const auto toSf = [](const QColor& c) {
        return sf::Color(static_cast<std::uint8_t>(c.red()),
            static_cast<std::uint8_t>(c.green()),
            static_cast<std::uint8_t>(c.blue()));
    };

    RenderingEngine::SelectionPalette palette; // renderer defaults; settings only override
    palette.object = toSf(_settings->getSelectionColor("object", toQ(palette.object)));
    palette.wall = toSf(_settings->getSelectionColor("wall", toQ(palette.wall)));
    palette.critter = toSf(_settings->getSelectionColor("critter", toQ(palette.critter)));
    palette.tile = toSf(_settings->getSelectionColor("tile", toQ(palette.tile)));

    _currentEditorWidget->setSelectionColors(palette);
}

void MainWindow::syncMenuStateToEditorWidget() {
    if (!_currentEditorWidget) {
        return;
    }

    // Sync visibility toggle states from menu to EditorWidget
    _currentEditorWidget->setShowObjects(_showObjectsAction->isChecked());
    _currentEditorWidget->setShowCritters(_showCrittersAction->isChecked());
    _currentEditorWidget->setShowWalls(_showWallsAction->isChecked());
    _currentEditorWidget->setShowRoof(_showRoofsAction->isChecked());
    _currentEditorWidget->setShowScrollBlk(_showScrollBlockersAction->isChecked());
    _currentEditorWidget->setShowWallBlockers(_showWallBlockersAction->isChecked());
    _currentEditorWidget->setShowHexGrid(_showHexGridAction->isChecked());
    _currentEditorWidget->setShowLightOverlays(_showLightOverlaysAction->isChecked());
    _currentEditorWidget->setShowExitGrids(_showExitGridsAction->isChecked());
    _currentEditorWidget->setMergeSelectionOutlines(_mergeOutlinesAction->isChecked());
    updateUndoRedoActions();

    // Reset selection to the default: all layers on (classic "All"), no special tool active.
    if (_currentEditorWidget && _floorLayerAction) {
        for (QAction* layer : { _floorLayerAction, _roofLayerAction, _objectsLayerAction }) {
            const QSignalBlocker block(layer); // apply once, below, rather than per toggle
            layer->setChecked(true);
        }
        applySelectionLayersFromMenu();
    }
}

void MainWindow::updateMapInfo(Map* map) {
    if (_mapInfoPanel) {
        _mapInfoPanel->setMap(map);
    }

    updateElevationMenu(map);

    if (_tilePalettePanel && map) {
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
        _elevation1Action->setDisabled(true);
        _elevation2Action->setDisabled(true);
        _elevation3Action->setDisabled(true);
        return;
    }

    // MAP header flag bits 0x2/0x4/0x8: a set bit means that elevation is DISABLED
    uint32_t map_flags = map->getMapFile().header.flags;
    bool hasElevation1 = ((map_flags & 0x2) == 0);
    bool hasElevation2 = ((map_flags & 0x4) == 0);
    bool hasElevation3 = ((map_flags & 0x8) == 0);

    _elevation1Action->setEnabled(hasElevation1);
    _elevation2Action->setEnabled(hasElevation2);
    _elevation3Action->setEnabled(hasElevation3);

    // Select the first available elevation
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
    if (!maybeSaveChanges()) {
        return; // user cancelled — keep the current map instead of loading over it
    }
    spdlog::info("MainWindow: Handling request to load map: {} (filesystem: {})", mapPath, forceFilesystem);

    auto loadingWidget = std::make_unique<LoadingWidget>(this);
    loadingWidget->setWindowTitle("Loading Map");

    // MapLoader is Qt-free; LoadingWidget owns it once added, so the callback uses
    // a handle to read its error message and present it here on the main thread.
    auto loaderHandle = std::make_shared<MapLoader*>(nullptr);
    auto mapLoader = std::make_unique<MapLoader>(_resourcesShared, mapPath, -1, forceFilesystem, [this, loaderHandle](auto map) {
        if (map) {
            auto editorWidget = std::make_unique<EditorWidget>(*_resourcesShared, std::move(map));
            setEditorWidget(std::move(editorWidget));
        } else if (*loaderHandle && (*loaderHandle)->hasError()) {
            QtDialogs::showError(this, "Missing Game Files",
                QString::fromStdString((*loaderHandle)->errorMessage()));
        }
    });
    *loaderHandle = mapLoader.get();
    loadingWidget->addLoader(std::move(mapLoader));

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
    auto& settings = *_settings;
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
    auto& settings = *_settings;

    // Restore window geometry first
    QByteArray geometry = settings.getWindowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    if (settings.getWindowMaximized()) {
        setWindowState(Qt::WindowMaximized);
    }

    QByteArray state = settings.getDockState();
    if (!state.isEmpty()) {
        restoreState(state);

        // Restore individual floating dock widget geometries after a short delay
        // This ensures the dock widgets are fully initialized first
        QTimer::singleShot(100, this, [this]() {
            auto& timerSettings = *_settings;

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

    auto& settings = *_settings;
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

    auto& settings = *_settings;

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
    _fileBrowserDock->show();
    _fileBrowserDock->raise();

    // File browser is tabified with the tile/object palettes; focusing it makes it the active tab
    _fileBrowserDock->widget()->setFocus();

    snapshotPanelVisibility();

    spdlog::debug("File browser panel shown and raised");
}

void MainWindow::closeCurrentMap() {
    if (!_currentEditorWidget) {
        return;
    }

    spdlog::info("Closing current map due to data path changes");

    stopGameLoop();

    _centralStack->removeWidget(_currentEditorWidget);
    _currentEditorWidget->deleteLater();
    _currentEditorWidget = nullptr;

    _centralStack->setCurrentWidget(_welcomeWidget);

    hidePanelsForNoMap();

    spdlog::debug("Current map closed successfully");
    updateUndoRedoActions();
}

bool MainWindow::hasActiveMap() const {
    return _currentEditorWidget != nullptr;
}

void MainWindow::showPreferences() {
    SettingsDialog dialog(_settings, this);

    connect(&dialog, &SettingsDialog::settingsSaved, this, [this](bool dataPathsChanged) {
        if (dataPathsChanged) {
            rebuildGameResourcesFromSettings();
        }
        applySelectionColorsToEditor();
    });

    int result = dialog.exec();

    if (result == QDialog::Accepted) {
        spdlog::info("Settings dialog closed with changes");
    } else {
        spdlog::info("Settings dialog cancelled");
    }

    showFileBrowserPanel();
}

void MainWindow::showAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::onPlayGame() {
    const Map::MapFile* mapFile = _currentEditorWidget ? &_currentEditorWidget->getMapFile() : nullptr;
    std::string mapFilename = _currentEditorWidget ? _currentEditorWidget->getMap()->filename() : "";

    if (!mapFilename.ends_with(".map")) {
        mapFilename += ".map";
    }

    _gameLauncher->playGame(mapFile, mapFilename);
}

void MainWindow::showSavePatternDialog() {
    if (!_currentEditorWidget || !hasActiveMap()) {
        showStatusMessage("Open a map and select objects or tiles first.");
        return;
    }
    auto* selectionManager = _currentEditorWidget->getSelectionManager();
    Map* map = _currentEditorWidget->getMap();
    if (!selectionManager || !map) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, "Save Selection as Pattern", pattern::PatternLibrary::rootDir(), "Gecko Pattern (*.json)");
    if (path.isEmpty()) {
        return;
    }

    const std::string name = QFileInfo(path).completeBaseName().toStdString();
    auto pattern = pattern::PatternBuilder::fromSelection(
        selectionManager->getCurrentSelection(), *map, _currentEditorWidget->getCurrentElevation(), name);
    if (!pattern.has_value()) {
        showStatusMessage("Nothing selected to save as a pattern.");
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        showStatusMessage("Failed to write pattern file.");
        return;
    }
    file.write(pattern::PatternSerializer::serialize(*pattern));
    showStatusMessage(QString("Saved pattern '%1' (%2 variant).")
            .arg(QString::fromStdString(pattern->name))
            .arg(pattern->variants.size()));
}

void MainWindow::showStampPatternDialog() {
    if (!_currentEditorWidget || !hasActiveMap()) {
        showStatusMessage("Open a map before stamping a pattern.");
        return;
    }

    PatternBrowserDialog dialog(*_resourcesShared, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    auto selected = dialog.selectedPattern();
    if (selected.has_value()) {
        _currentEditorWidget->beginStampPattern(std::move(*selected));
    }
}

void MainWindow::showMapBrowserDialog() {
    MapBrowserDialog dialog(*_resourcesShared, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString mapPath = dialog.selectedMapPath();
    if (!mapPath.isEmpty()) {
        handleMapLoadRequest(mapPath.toStdString(), false);
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

} // namespace geck
