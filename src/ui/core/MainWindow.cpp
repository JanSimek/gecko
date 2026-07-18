#define QT_NO_EMIT
#include "MainWindow.h"
#include "Application.h"
#include "EditorWidget.h"
#include "format/map/MapScript.h"
#include "util/GameDataPathResolver.h"
#include "ui/widgets/LoadingWidget.h"
#include "ui/widgets/WelcomeWidget.h"
#include "ui/widgets/SFMLWidget.h"
#include "ui/panels/SelectionPanel.h"
#include "ui/panels/MapInfoPanel.h"
#include "ui/panels/ScriptsPanel.h"
#include "ui/panels/TilePalettePanel.h"
#include "ui/panels/ObjectPalettePanel.h"
#include "ui/panels/FileBrowserPanel.h"
#include "ui/panels/LogPanel.h"
#include "ui/panels/CompletenessView.h"
#include "resource/MapCompleteness.h"
#include "ui/rendering/ThumbnailPrewarmer.h"
#ifdef GECK_SCRIPTING_ENABLED
#include "ui/panels/ScriptConsoleWidget.h"
#include "scripting/LuaScriptRuntime.h" // ScriptResult
#endif
#include "ui/tiles/TilePlacementManager.h"
#include "ui/tools/ExitGridPlacementManager.h"
#include "ui/tools/FillBrushTool.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/FillDialog.h"
#include "ui/dialogs/SpatialScriptDialog.h"
#include "ui/dialogs/ScriptSelectorDialog.h"
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
#include "ui/QtDialogs.h"
#include "ui/ExternalEditorLauncher.h"
#include "ui/ScriptSourceService.h"
#include "reader/lst/LstReader.h"
#include "format/lst/Lst.h"
#include "format/map/MapObject.h"
#include "editor/Object.h"
#include "ui/IconHelper.h"

#include <chrono>
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
#include <QToolButton>
#include <QIcon>
#include <QSignalBlocker>
#include <QTabWidget>
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
    , _scriptsDock(nullptr)
    , _selectionDock(nullptr)
    , _tilePaletteDock(nullptr)
    , _objectPaletteDock(nullptr)
    , _fileBrowserDock(nullptr)
    , _selectionPanel(nullptr)
    , _mapInfoPanel(nullptr)
    , _scriptsPanel(nullptr)
    , _tilePalettePanel(nullptr)
    , _objectPalettePanel(nullptr)
    , _fileBrowserPanel(nullptr)
    , _mapInfoPanelAction(nullptr)
    , _scriptsPanelAction(nullptr)
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
    _scriptSourceService = std::make_unique<ScriptSourceService>(*_resourcesShared, _settings, *_externalEditorLauncher, this);

    restoreDockWidgetState();

    // restoreDockWidgetState() seeded _restoredDockState with the working layout; hide the panels
    // until a map is loaded (welcome screen). showPanelsForMap() re-applies the layout on map open.
    hidePanelsForNoMap();

    // Reflect actual visibility once restoration settles
    QTimer::singleShot(200, this, &MainWindow::updatePanelMenuActions);

    connect(_gameLoopTimer, &QTimer::timeout, this, &MainWindow::updateAndRender);
}

MainWindow::~MainWindow() {
    if (_thumbnailPrewarmer) {
        _thumbnailPrewarmer->requestStop(); // its destructor (QObject child) waits for the thread
    }
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
    {
        const auto initStart = std::chrono::steady_clock::now();
        _currentEditorWidget->init();
        spdlog::info("EditorWidget::init (map view build) took {}ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - initStart).count());
    }

    connectToEditorWidget();
    connect(_currentEditorWidget, &EditorWidget::undoStackChanged, this, &MainWindow::updateUndoRedoActions);
    // Any edit (or undo/redo) marks the map dirty for the close/title prompts.
    connect(_currentEditorWidget, &EditorWidget::undoStackChanged, this, [this]() { setMapModified(true); });
    // Non-undoable Console-script mutations (newMap/setPlayerStart) flag the map dirty too.
    // A script/fill can reference new protos or tiles wholesale, so re-check completeness as well.
    connect(_currentEditorWidget, &EditorWidget::mapModifiedByScript, this, [this]() {
        setMapModified(true);
        refreshCompleteness();
    });

    syncMenuStateToEditorWidget();

    if (_currentEditorWidget->getMap()) {
        updateMapInfo(_currentEditorWidget->getMap());
        showPanelsForMap();
    } else {
        hidePanelsForNoMap();
    }

    // A freshly loaded/created map starts clean; show its name in the title bar.
    _mapModified = false;
    updateWindowTitle();

    // Installing a map must (re)start the render loop: closeCurrentMap() stops it, and it is
    // otherwise only started once at application startup. startGameLoop() is idempotent, so this is
    // a no-op when the loop is already running (e.g. the startup map load).
    startGameLoop();

    QTimer::singleShot(50, this, &MainWindow::updatePanelMenuActions);
    updateUndoRedoActions();
    updateFillSelectionAction();
    refreshCompleteness();
}

void MainWindow::setupUI() {
    _centralStack = new QStackedWidget(this);
    setCentralWidget(_centralStack);

    // Welcome widget is shown when no map is loaded; its buttons reuse the File-menu handlers.
    _welcomeWidget = new WelcomeWidget(this);
    connect(_welcomeWidget, &WelcomeWidget::newMapRequested, this, &MainWindow::newMapRequested);
    connect(_welcomeWidget, &WelcomeWidget::browseMapsRequested, this, &MainWindow::showMapBrowserDialog);
    connect(_welcomeWidget, &WelcomeWidget::preferencesRequested, this, &MainWindow::showPreferences);
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
            // setEditorWidget scanned before createNewMap populated the map; re-check now.
            refreshCompleteness();
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
    connect(this, &MainWindow::closeMapRequested, [this]() {
        if (!hasActiveMap()) {
            return; // nothing open — the welcome screen is already showing
        }
        if (!maybeSaveChanges()) {
            return; // user cancelled the close to keep their unsaved map
        }
        closeCurrentMap();
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
        if (_suppressDockStateSave) {
            return;
        }

        spdlog::debug("{} action toggled: {}", dock->windowTitle().toStdString(), visible);
        // showDock() flips the dock, which fires QDockWidget::visibilityChanged below; that handler
        // persists the new layout, so there's nothing to save here.
        showDock(dock, visible);
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
        // A genuine user show/hide updates the persisted dock layout immediately, so it survives even
        // a non-clean exit (e.g. the app being killed to recompile). saveDockWidgetState() is a no-op
        // while re-laying-out programmatically or when no map is open.
        if (!_suppressDockStateSave) {
            saveDockWidgetState();
        }
    });

    return action;
}

std::array<QDockWidget*, 6> MainWindow::managedDocks() const {
    return { _mapInfoDock, _scriptsDock, _selectionDock, _tilePaletteDock, _objectPaletteDock, _fileBrowserDock };
}

std::array<MainWindow::DockActionPair, 6> MainWindow::managedDockActionPairs() const {
    return { {
        { _mapInfoDock, _mapInfoPanelAction },
        { _scriptsDock, _scriptsPanelAction },
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

    const std::array<DockPlacement, 6> placements = { {
        { _mapInfoDock, Qt::RightDockWidgetArea },
        { _scriptsDock, Qt::RightDockWidgetArea },
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
    // Tab the Scripts panel behind Map Info; keep Map Info the visible tab by default.
    if (_mapInfoDock && _scriptsDock) {
        tabifyDockWidget(_mapInfoDock, _scriptsDock);
        _mapInfoDock->raise();
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

    // Give each dock column enough width to show its panel content (e.g. the Map Info form) without a
    // horizontal scrollbar. Only applied to the default layout; a restored layout keeps the user's own
    // widths, so this never fights a manual resize.
    const int preferredWidth = ui::constants::sizes::PANEL_PREFERRED_WIDTH;
    resizeDocks({ _mapInfoDock, _tilePaletteDock }, { preferredWidth, preferredWidth }, Qt::Horizontal);
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
    addMenuAction(_fileMenu, ":/icons/actions/close.svg", "&Close Map", &MainWindow::closeMapRequested, QKeySequence::Close, "Close the current map and return to the welcome screen");

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
#ifdef GECK_SCRIPTING_ENABLED
    // Fill is driven entirely by Luau fill scripts, so it is offered only when scripting is built in.
    // Every use of _fillSelectionAction below is null-guarded, so leaving it null disables the feature.
    _fillSelectionAction = addMenuAction(_editMenu, ":/icons/actions/paint.svg", "&Fill Selection...", &MainWindow::showFillDialog, QKeySequence(), "Fill the selected area with a Luau fill script");
#endif

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
            refreshScriptsPanel();  // an undone spatial add/edit/delete must re-appear in the panel
            refreshMapEdgesPanel(); // and an undone edge edit must re-sync the Map Edges group
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
            refreshScriptsPanel();  // keep the panel in step with a redone spatial add/edit/delete
            refreshMapEdgesPanel(); // and with a redone edge edit
        }
    });

    updateUndoRedoActions();
    updateFillSelectionAction();

    // File-level SSL toolchain actions (sslc / int2ssl). Per-script "Edit Script Source" lives on
    // the Scripts panel rows, the Map Info map-script row and the spatial-script dialog.
    _scriptsMenu = _menuBar->addMenu("&Scripts");
    addMenuAction(_scriptsMenu, ":/icons/actions/save.svg", "&Compile Script...", &MainWindow::showCompileScriptDialog, QKeySequence(), "Compile an SSL source file to the .int bytecode the engine loads");
    addMenuAction(_scriptsMenu, ":/icons/actions/open.svg", "&Decompile Script...", &MainWindow::showDecompileScriptDialog, QKeySequence(), "Recover best-effort SSL source from a compiled .int script");

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

    const std::array<ViewToggleSpec, 12> viewToggleSpecs = { {
        { &_showObjectsAction, ":/icons/actions/view-objects.svg", "Show &Objects", UI::DEFAULT_SHOW_OBJECTS, &MainWindow::showObjectsToggled, {}, {} },
        { &_showCrittersAction, ":/icons/actions/view-critters.svg", "Show &Critters", UI::DEFAULT_SHOW_CRITTERS, &MainWindow::showCrittersToggled, {}, {} },
        { &_showWallsAction, ":/icons/actions/view-walls.svg", "Show &Walls", UI::DEFAULT_SHOW_WALLS, &MainWindow::showWallsToggled, {}, {} },
        { &_showRoofsAction, ":/icons/actions/view-roofs.svg", "Show &Roofs", UI::DEFAULT_SHOW_ROOF, &MainWindow::showRoofsToggled, {}, {} },
        { &_showScrollBlockersAction, ":/icons/actions/view-scroll-blockers.svg", "Show Scroll &Blockers", UI::DEFAULT_SHOW_SCROLL_BLK, &MainWindow::showScrollBlockersToggled, {}, {} },
        { &_showWallBlockersAction, ":/icons/actions/view-wall-blockers.svg", "Show &Wall Blockers", UI::DEFAULT_SHOW_WALL_BLK, &MainWindow::showWallBlockersToggled, {}, {} },
        { &_showHexGridAction, ":/icons/actions/view-grid.svg", "Show &Hex Grid", UI::DEFAULT_SHOW_HEX_GRID, &MainWindow::showHexGridToggled, {}, {} },
        { &_showLightOverlaysAction, ":/icons/actions/view-light.svg", "Show &Light Overlays", false, &MainWindow::showLightOverlaysToggled, {}, {} },
        { &_showExitGridsAction, ":/icons/actions/view-exits.svg", "Show &Exit Grids", false, &MainWindow::showExitGridsToggled, "Show exit grid markers", QKeySequence("Ctrl+E") },
        { &_showSpatialScriptsAction, ":/icons/actions/target-arrow.svg", "Show Spatial &Scripts", false, &MainWindow::showSpatialScriptsToggled, "Show spatial-script trigger markers and their radius", {} },
        { &_showMapEdgesAction, ":/icons/actions/map-edges.svg", "Show Map &Edges", false, &MainWindow::showMapEdgesToggled, "Show the .edg scroll-boundary zones and clip rect", {} },
        { &_showUnreachableAction, ":/icons/actions/view-unreachable.svg", "Highlight &Unreachable Areas", false, &MainWindow::showUnreachableToggled, "Shade walkable hexes cut off from the player start and every exit grid", {} },
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

    _edgeScrollAction = _viewMenu->addAction("&Edge Scrolling");
    _edgeScrollAction->setCheckable(true);
    _edgeScrollAction->setChecked(_settings ? _settings->getEdgeScrollEnabled() : true);
    _edgeScrollAction->setStatusTip("Auto-scroll the map when the cursor rests near a viewport edge");
    connect(_edgeScrollAction, &QAction::toggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setEdgeScrollEnabled(enabled);
        if (_settings) {
            _settings->setEdgeScrollEnabled(enabled);
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
        // managedDocks() is the single source of truth for the dock set, so this can't drift as panels
        // are added, and it tolerates any not-yet-created dock.
        for (QDockWidget* dock : managedDocks()) {
            if (dock != nullptr && dock->isFloating()) {
                dock->setFloating(false);
            }
        }
        spdlog::debug("Re-docked all floating panels");
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

    // "Fill Selection…" (the Edit-menu action) also on the toolbar, beside the tool modes.
    if (_fillSelectionAction) {
        _mainToolBar->addAction(_fillSelectionAction);
    }

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

    // Unified Exit-Grids tool: one checkable button plus a dropdown choosing the sub-mode
    // ("Place single hex" vs "Draw edge"), mirroring the selection-mode dropdown above.
    _exitGridsAction = _mainToolBar->addAction(createIcon(":/icons/actions/door-exit.svg"), "Exit Grids");
    _exitGridsAction->setStatusTip("Place exit-grid markers (single hex) or draw an edge line along the map border");
    _exitGridsAction->setCheckable(true);

    _exitGridsMenu = new QMenu(this);
    auto addExitGridMode = [this](const QString& label, EditorMode mode, bool checked) {
        QAction* action = _exitGridsMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(checked);
        action->setData(static_cast<int>(mode));
        connect(action, &QAction::triggered, this, [this, mode]() {
            // Exclusive: tick the chosen sub-mode, untick the other, and (re)activate the tool.
            for (QAction* item : _exitGridsMenu->actions()) {
                const QSignalBlocker block(item);
                item->setChecked(item->data().toInt() == static_cast<int>(mode));
            }
            const QSignalBlocker buttonBlock(_exitGridsAction);
            _exitGridsAction->setChecked(true);
            applyExitGridsTool(true);
        });
        return action;
    };
    _exitGridDrawRegionAction = addExitGridMode("Draw edge", EditorMode::MarkExits, true);
    _exitGridPlaceHexAction = addExitGridMode("Place single hex", EditorMode::PlaceExitGrid, false);

    // Attach the sub-mode dropdown to the button's menu-indicator arrow (MenuButtonPopup): the main
    // button is a plain on/off toggle, while the small arrow opens the dropdown to switch sub-mode.
    _exitGridsAction->setMenu(_exitGridsMenu);
    if (auto* toolButton = qobject_cast<QToolButton*>(_mainToolBar->widgetForAction(_exitGridsAction))) {
        toolButton->setPopupMode(QToolButton::MenuButtonPopup);
    }

    // Toggling the button on activates the chosen sub-mode; off returns to Select.
    connect(_exitGridsAction, &QAction::triggered, this, [this](bool checked) {
        applyExitGridsTool(checked);
    });

    // Freehand fill brush: drag-to-paint the tile palette's selection; one stroke = one undo
    // entry. Runs as a registered tool (EditorMode::PluginTool + ToolRegistry).
    _fillBrushAction = _mainToolBar->addAction(createIcon(":/icons/actions/paint.svg"), "Fill Brush");
    _fillBrushAction->setStatusTip("Paint the selected palette tile by dragging (one stroke is one undo step)");
    _fillBrushAction->setCheckable(true);
    connect(_fillBrushAction, &QAction::triggered, this, [this](bool checked) {
        applyFillBrushTool(checked);
    });
}

void MainWindow::applyFillBrushTool(bool checked) {
    if (!_currentEditorWidget) {
        return;
    }
    if (!checked) {
        _currentEditorWidget->setMode(EditorMode::Select);
        return;
    }
    const bool hasTile = _tilePalettePanel && _tilePalettePanel->hasSelectedTile();
    if (!hasTile || !_currentEditorWidget->activateFillBrush(_tilePalettePanel->getSelectedTileIndex(), _tilePalettePanel->isRoofMode())) {
        // No tile loaded: don't enter a brush that can't paint. Revert the toggle and say why.
        const QSignalBlocker blocker(_fillBrushAction);
        _fillBrushAction->setChecked(false);
        showStatusMessage("Fill brush: select a tile in the Tiles palette first");
        return;
    }
    updateModeDisplay("Mode: Fill brush", ":/icons/actions/paint.svg");
}

void MainWindow::applyExitGridsTool(bool checked) {
    if (!_currentEditorWidget) {
        return;
    }
    if (!checked) {
        _currentEditorWidget->setMode(EditorMode::Select);
        return;
    }
    // Activate whichever sub-mode is ticked in the dropdown (default: Draw edge).
    EditorMode mode = EditorMode::MarkExits;
    for (const QAction* item : _exitGridsMenu->actions()) {
        if (item->isChecked()) {
            mode = static_cast<EditorMode>(item->data().toInt());
            break;
        }
    }
    _currentEditorWidget->setMode(mode);
    _exitGridsAction->setText(mode == EditorMode::PlaceExitGrid ? "Place Hex" : "Draw Edge");
}

void MainWindow::syncToolModeActions(EditorMode mode) {
    const auto sync = [mode](QAction* action, EditorMode actionMode) {
        if (action) {
            QSignalBlocker blocker(action);
            action->setChecked(mode == actionMode);
        }
    };
    sync(_selectToolAction, EditorMode::Select);

    // The brush button tracks its specific registered tool, not PluginTool as a whole —
    // object placement also runs as PluginTool and must not light the brush up.
    if (_fillBrushAction) {
        const QSignalBlocker blocker(_fillBrushAction);
        _fillBrushAction->setChecked(mode == EditorMode::PluginTool && _currentEditorWidget
            && _currentEditorWidget->activeToolId() == FillBrushTool::ID);
    }

    // Unified Exit-Grids button is checked while in either sub-mode; the dropdown shows which one,
    // and the button label tracks the active sub-mode.
    const bool inExitGridMode = (mode == EditorMode::PlaceExitGrid || mode == EditorMode::MarkExits);
    if (_exitGridsAction) {
        QSignalBlocker blocker(_exitGridsAction);
        _exitGridsAction->setChecked(inExitGridMode);
        if (mode == EditorMode::PlaceExitGrid) {
            _exitGridsAction->setText("Place Hex");
        } else if (mode == EditorMode::MarkExits) {
            _exitGridsAction->setText("Draw Edge");
        } else {
            _exitGridsAction->setText("Exit Grids");
        }
    }
    if (_exitGridsMenu && inExitGridMode) {
        for (QAction* item : _exitGridsMenu->actions()) {
            const QSignalBlocker block(item);
            item->setChecked(item->data().toInt() == static_cast<int>(mode));
        }
    }

    // Free up "R" for the viewport while stamping or while a registered tool runs (object
    // placement); otherwise the toolbar shortcut would swallow the key before it reaches the editor.
    if (_rotateAction) {
        _rotateAction->setEnabled(mode != EditorMode::StampPattern && mode != EditorMode::PluginTool);
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
}
#endif

void MainWindow::setLogModel(LogModel* model) {
    if (_logPanel) {
        _logPanel->setModel(model);
    }
}

QMenu* MainWindow::ensurePluginMenu() {
    if (!_pluginMenu && _menuBar) {
        // Keep Help rightmost, per platform convention: insert Plugins before it.
        _pluginMenu = new QMenu(tr("&Plugins"), _menuBar);
        if (_helpMenu) {
            _menuBar->insertMenu(_helpMenu->menuAction(), _pluginMenu);
        } else {
            _menuBar->addMenu(_pluginMenu);
        }
    }
    return _pluginMenu;
}

QAction* MainWindow::addPluginMenuItem(const QString& id, const QString& text) {
    // Validate before ensurePluginMenu() so a rejected registration can't leave an empty
    // Plugins menu in the menu bar.
    if (id.isEmpty() || text.isEmpty() || _pluginUi.contains(id)) {
        return nullptr;
    }
    QMenu* menu = ensurePluginMenu();
    if (!menu) {
        return nullptr;
    }

    QAction* action = menu->addAction(text);
    menu->menuAction()->setVisible(true);
    _pluginUi.insert(id, PluginUiRegistration{
                             .kind = PluginUiRegistration::Kind::MenuAction,
                             .action = action,
                             .menu = menu,
                         });
    return action;
}

QAction* MainWindow::addPluginToolButton(const QString& id, const QString& text, const QIcon& icon) {
    if (id.isEmpty() || text.isEmpty() || !_mainToolBar || _pluginUi.contains(id)) {
        return nullptr;
    }

    QAction* action = icon.isNull() ? _mainToolBar->addAction(text) : _mainToolBar->addAction(icon, text);
    _pluginUi.insert(id, PluginUiRegistration{
                             .kind = PluginUiRegistration::Kind::ToolBarAction,
                             .action = action,
                         });
    return action;
}

QDockWidget* MainWindow::addPluginDock(const QString& id, const QString& title, QWidget* widget,
    Qt::DockWidgetArea area) {
    if (id.isEmpty() || title.isEmpty() || !widget || _pluginUi.contains(id)) {
        return nullptr;
    }

    QDockWidget* dock = new QDockWidget(title, this);
    dock->setObjectName(QStringLiteral("PluginDock_%1").arg(id));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
    dock->setWidget(widget);
    addDockWidget(area, dock);
    dock->hide();

    QAction* toggleAction = dock->toggleViewAction();
    toggleAction->setText(title);
    if (_viewMenu) {
        _viewMenu->addAction(toggleAction);
    }
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) {
        if (!_suppressDockStateSave) {
            saveDockWidgetState();
        }
    });

    _pluginUi.insert(id, PluginUiRegistration{
                             .kind = PluginUiRegistration::Kind::Dock,
                             .action = toggleAction,
                             .dock = dock,
                         });
    return dock;
}

void MainWindow::removePluginUi(const QString& id) {
    const auto it = _pluginUi.find(id);
    if (it == _pluginUi.end()) {
        return;
    }

    const PluginUiRegistration registration = it.value();
    _pluginUi.erase(it);

    switch (registration.kind) {
        case PluginUiRegistration::Kind::MenuAction:
            if (registration.menu && registration.action) {
                registration.menu->removeAction(registration.action);
            }
            if (registration.action) {
                registration.action->deleteLater();
            }
            // An empty Plugins menu is just noise in the menu bar; hide it until the next
            // registration (the QMenu itself stays alive so pointers remain stable).
            if (_pluginMenu && _pluginMenu->isEmpty()) {
                _pluginMenu->menuAction()->setVisible(false);
            }
            break;
        case PluginUiRegistration::Kind::ToolBarAction:
            if (_mainToolBar && registration.action) {
                _mainToolBar->removeAction(registration.action);
            }
            if (registration.action) {
                registration.action->deleteLater();
            }
            break;
        case PluginUiRegistration::Kind::Dock:
            if (_viewMenu && registration.action) {
                _viewMenu->removeAction(registration.action);
            }
            if (registration.dock) {
                removeDockWidget(registration.dock);
                registration.dock->deleteLater();
            }
            break;
    }
}

void MainWindow::startThumbnailPrewarm() {
    if (_thumbnailPrewarmer) {
        _thumbnailPrewarmer->requestStop();
        _thumbnailPrewarmer->wait();
        _thumbnailPrewarmer->deleteLater();
        _thumbnailPrewarmer = nullptr;
    }

    auto dataPaths = _settings->getDataPaths();
    util::ensureFallbackDataPath(dataPaths, Application::getResourcesPath());
    if (dataPaths.empty()) {
        return;
    }

    _thumbnailPrewarmer = new ThumbnailPrewarmer(std::move(dataPaths), 128, this);
    _thumbnailPrewarmer->start(QThread::LowestPriority);
}

void MainWindow::refreshCompleteness() {
    if (!_completenessView) {
        return;
    }

    Map* map = _currentEditorWidget ? _currentEditorWidget->getMap() : nullptr;
    if (!map) {
        _completenessView->clearReport();
        return;
    }

    const auto report = resource::scanMapCompleteness(*_resourcesShared, *map);
    _completenessView->setReport(report, QString::fromStdString(map->filename()));

    // A one-line total in the Log tab (and on stderr) so the gaps are noticed even when the
    // Map tab isn't the one showing; the tab holds the per-entry breakdown.
    if (!report.complete()) {
        spdlog::warn("Map completeness: {} references {} unresolved resource(s) — {} tile(s), "
                     "{} object sprite(s), {} script(s); see the Log panel's Map tab",
            map->filename(), report.missingCount(), report.missingTiles.size(),
            report.missingObjectArt.size(), report.unresolvedScripts.size());
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

    _mapInfoPanel = new MapInfoPanel(*_resourcesShared, _settings);
    _mapInfoDock = createDock("Map Information", "MapInfoDock", _mapInfoPanel, Qt::RightDockWidgetArea, QSizePolicy::Preferred, ui::constants::dock::MIN_HEIGHT_SMALL);

    _selectionPanel = new SelectionPanel(*_resourcesShared);
    _selectionDock = createDock("Selection", "SelectionDock", _selectionPanel, Qt::RightDockWidgetArea, QSizePolicy::Preferred, ui::constants::dock::MIN_HEIGHT_SMALL);

    _scriptsPanel = new ScriptsPanel(*_resourcesShared);
    _scriptsDock = createDock("Scripts", "ScriptsDock", _scriptsPanel, Qt::RightDockWidgetArea, QSizePolicy::Preferred, ui::constants::dock::MIN_HEIGHT_SMALL);

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

    // Log & diagnostics: same shape as the script console — a bottom dock outside the managed
    // layout persistence, hidden until opened from the View menu. Two tabs: the raw record
    // stream (the application's LogModel, attached via setLogModel() once the window exists)
    // and the structured per-map completeness summary (refreshed on map load/close).
    _logPanel = new LogPanel();
    _completenessView = new CompletenessView();
    connect(_completenessView, &CompletenessView::refreshRequested, this, &MainWindow::refreshCompleteness);
    auto* logTabs = new QTabWidget();
    logTabs->setDocumentMode(true);
    logTabs->addTab(_logPanel, tr("Log"));
    logTabs->addTab(_completenessView, tr("Map"));
    _logDock = createDock("Log", "LogDock", logTabs, Qt::BottomDockWidgetArea, QSizePolicy::Expanding, ui::constants::dock::MIN_HEIGHT_SMALL);
    _logDock->hide();

    if (_viewMenu) {
        _viewMenu->addSeparator();
#ifdef GECK_SCRIPTING_ENABLED
        QAction* consoleAction = _scriptConsoleDock->toggleViewAction();
        consoleAction->setText(tr("Script &Console"));
        _viewMenu->addAction(consoleAction);
#endif
        QAction* logAction = _logDock->toggleViewAction();
        logAction->setText(tr("&Log"));
        _viewMenu->addAction(logAction);
    }

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
    connect(this, &MainWindow::showSpatialScriptsToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowSpatialScripts(enabled);
    });
    connect(this, &MainWindow::showUnreachableToggled, this, [this](bool enabled) {
        if (_currentEditorWidget)
            _currentEditorWidget->setShowUnreachableAreas(enabled);
    });
    connect(this, &MainWindow::showMapEdgesToggled, this, [this](bool enabled) {
        if (_currentEditorWidget) {
            _currentEditorWidget->setShowMapEdges(enabled);
            // Hiding the overlay drops the edge-zone selection, so re-sync the panel's button state.
            refreshMapEdgesPanel();
        }
    });
    connect(this, &MainWindow::elevationChanged, this, [this](int elevation) {
        if (_currentEditorWidget) {
            _currentEditorWidget->changeElevation(elevation);
            refreshMapEdgesPanel(); // edge zones/clip are per-elevation, so re-sync the panel
        }
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
    spdlog::debug("Dock widgets configured with proper z-order and panel features");
}

void MainWindow::connectFileBrowserSignals() {
    if (!_fileBrowserPanel) {
        return;
    }

    connect(_fileBrowserPanel, &FileBrowserPanel::fileDoubleClicked, this, [this](const QString& filePath) {
        if (filePath.endsWith(".map", Qt::CaseInsensitive)) {
            spdlog::debug("MainWindow: Opening map from file browser: {}", filePath.toStdString());
            handleMapLoadRequest(filePath.toStdString(), false);
        } else if (ExternalEditorLauncher::isTextFile(filePath)) {
            spdlog::debug("MainWindow: Opening text file from file browser: {}", filePath.toStdString());
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
        // deleteLater, not delete: the old panel may still be on the call stack (FileBrowserPanel's
        // chunked tree build pumps the event loop mid-chunk); a synchronous delete here would free
        // an object that resumes executing when the loop unwinds.
        oldWidget->deleteLater();
    }

    dock->setWidget(panel);
}

void MainWindow::rebuildResourcePanels() {
    _mapInfoPanel = new MapInfoPanel(*_resourcesShared, _settings);
    replaceDockPanelWidget(_mapInfoDock, _mapInfoPanel, QSizePolicy::Preferred);

    _scriptsPanel = new ScriptsPanel(*_resourcesShared);
    replaceDockPanelWidget(_scriptsDock, _scriptsPanel, QSizePolicy::Preferred);

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

    auto dataPaths = _settings->getDataPaths();
    // Same fallback as Application::loadDataPaths: the editor's bundled resources (blank tile,
    // overlay art, ...) must survive any data-path reconfiguration, or every subsequent map load
    // fails on editor-essential art the game data does not ship.
    util::ensureFallbackDataPath(dataPaths, Application::getResourcesPath());

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
    startThumbnailPrewarm(); // new mounts can mean new maps and new source identities
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

    // Permanent contextual key-hint. A permanent widget so transient _statusLabel messages
    // (showStatusMessage) don't clobber it; EditorWidget::hintChanged drives its text.
    _hintLabel = new QLabel();
    _statusBar->addPermanentWidget(_hintLabel);

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
        spdlog::debug("Qt6 game loop started");
    }
}

void MainWindow::stopGameLoop() {
    if (_isRunning) {
        _isRunning = false;
        _gameLoopTimer->stop();
        spdlog::debug("Qt6 game loop stopped");
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
    const auto root = _settings->resolveWritableDataPath();
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
        _mapInfoPanel->persistMapVars();  // flush any edited global variable values to the map's .gam
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
    // Persist the layout on a normal quit; the destructor runs too late/unreliably (skipped entirely
    // when the process is killed). saveDockWidgetState() no-ops when no map is open (welcome screen).
    saveDockWidgetState();
    _suppressDockStateSave = true; // teardown: closing docks must not rewrite the saved layout
    stopGameLoop();
    event->accept();
}

void MainWindow::raiseObjectPalette() {
    if (_objectPaletteDock) {
        _objectPaletteDock->show();
        _objectPaletteDock->raise();
    }
}

void MainWindow::raiseTilePalette() {
    if (_tilePaletteDock) {
        _tilePaletteDock->show();
        _tilePaletteDock->raise();
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Editor shortcuts that act on the SFML viewport (e.g. the "P" eyedropper) are handled by the
    // InputHandler on the forwarded SFML key stream, not here — a focused child consumes key events
    // before they reach this override. This just forwards keys to SFML.
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
                    // Picking a tile while the fill brush is active re-loads the brush in
                    // place instead of yanking the user back to click-painting.
                    if (_currentEditorWidget->activeToolId() == FillBrushTool::ID) {
                        _currentEditorWidget->activateFillBrush(tileIndex, isRoof);
                        return;
                    }
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
                    spdlog::debug("MainWindow: Reloaded sprites for newly added elevation {}", elevation);
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
                    spdlog::debug("MainWindow: Switched away from removed elevation {}", elevation);
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
            this, [this]() { openSpatialScriptDialog(std::nullopt); });

        // Map-edge (.edg) editing routed to the editor's EdgeEditService (undoable).
        connect(_mapInfoPanel, &MapInfoPanel::addEdgeZoneRequested, this, [this]() {
            if (!_currentEditorWidget) {
                return;
            }
            // Make the overlay visible so the new zone and its drag handles can be seen and shaped.
            if (_showMapEdgesAction && !_showMapEdgesAction->isChecked()) {
                _showMapEdgesAction->setChecked(true); // cascades to setShowMapEdges
            }
            _currentEditorWidget->addEdgeZone();
        });
        connect(_mapInfoPanel, &MapInfoPanel::deleteEdgeZoneRequested, this, [this]() {
            if (_currentEditorWidget) {
                _currentEditorWidget->deleteSelectedEdgeZone();
            }
        });
        connect(_mapInfoPanel, &MapInfoPanel::upgradeEdgeVersion2Requested, this, [this]() {
            if (_currentEditorWidget) {
                _currentEditorWidget->upgradeMapEdgeToVersion2();
            }
        });
        connect(_mapInfoPanel, &MapInfoPanel::edgeClipToggled, this, [this](int side) {
            if (_currentEditorWidget) {
                _currentEditorWidget->toggleEdgeClipSide(side);
            }
        });
        connect(_mapInfoPanel, &MapInfoPanel::resetEdgeSquareRequested, this, [this]() {
            if (_currentEditorWidget) {
                _currentEditorWidget->resetMapEdgeSquare();
            }
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
        // Editing a global variable value edits the map's .gam (MAP_GLOBAL_VARS); flag the map modified
        // so persistMapVars() writes the .gam alongside the .map on save (see the saveMap handlers).
        connect(_mapInfoPanel, &MapInfoPanel::mapVariablesChanged, this, [this]() { setMapModified(true); });
        // "Edit Source..." next to the map-script row opens the header script's .ssl.
        connect(_mapInfoPanel, &MapInfoPanel::mapScriptSourceEditRequested, this, [this](int programIndex) {
            if (_scriptSourceService) {
                _scriptSourceService->editScriptSource(programIndex);
            }
        });
    }

    // ScriptsPanel signals → current editor widget. Double-clicking an object-owned script row jumps to
    // (selects + centers on) the owning object; ownerless rows report that there's no object to reveal.
    if (_scriptsPanel) {
        connect(_scriptsPanel, &ScriptsPanel::scriptObjectActivated,
            this, [this](int sid) {
                if (!_currentEditorWidget) {
                    return;
                }
                if (_currentEditorWidget->revealScriptObject(sid)) {
                    // The reveal may have switched elevation directly; re-sync the elevation menu.
                    updateElevationMenu(_currentEditorWidget->getMap());
                } else {
                    showStatusMessage("Script has no object on the map");
                }
            });

        // Selecting a spatial-script row drives the shared selection (highlights its marker on the map).
        connect(_scriptsPanel, &ScriptsPanel::spatialScriptSelected, this, [this](uint32_t sid) {
            if (_currentEditorWidget) {
                _currentEditorWidget->setSelectedSpatialScript(sid);
            }
        });
        // Edit / delete requested from the panel (context menu or double-click).
        connect(_scriptsPanel, &ScriptsPanel::spatialScriptEditRequested, this,
            [this](uint32_t sid) { openSpatialScriptDialog(sid); });
        connect(_scriptsPanel, &ScriptsPanel::spatialScriptDeleteRequested, this, [this](uint32_t sid) {
            if (_currentEditorWidget) {
                _currentEditorWidget->deleteSpatialScript(sid);
            }
        });
        // "Edit Script Source..." on any row: open the .ssl behind the row's program index.
        connect(_scriptsPanel, &ScriptsPanel::scriptSourceEditRequested, this, [this](int programIndex) {
            if (_scriptSourceService) {
                _scriptSourceService->editScriptSource(programIndex);
            }
        });
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

    // Keep "Fill Selection…" enabled in step with the selection (it needs a fillable layer).
    connect(_currentEditorWidget, &EditorWidget::selectionChanged, this,
        [this](const selection::SelectionState&, int) { updateFillSelectionAction(); });
    updateFillSelectionAction();

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
    // Contextual key-hint: the editor emits hintChanged whenever the mode or selection
    // changes; show it on the permanent status-bar label (never clobbered by transient messages).
    connect(_currentEditorWidget, &EditorWidget::hintChanged, this, [this](const QString& hint) {
        if (_hintLabel) {
            _hintLabel->setText(hint);
        }
    });
    // The editor widget doesn't emit editorModeChanged on connect, so seed the
    // toolbar state from its current mode (e.g. after switching maps).
    syncToolModeActions(_currentEditorWidget->currentMode());
    // Likewise seed the hint once, since hintChanged isn't emitted on connect either.
    if (_hintLabel) {
        _hintLabel->setText(_currentEditorWidget->currentHintText());
    }
    connect(_currentEditorWidget, &EditorWidget::hexHoverChanged, this, &MainWindow::updateHexIndexDisplay);
    connect(_currentEditorWidget, &EditorWidget::mapLoadRequested, this, [this](const std::string& mapPath) {
        handleMapLoadRequest(mapPath, true);
    });

    // Spatial-script selection/editing sync (map <-> Scripts panel). The panel mirrors the map-side
    // selection; a map double-click opens the editor; add/edit/delete repopulate the panel.
    connect(_currentEditorWidget, &EditorWidget::spatialScriptSelectionChanged, this, [this](uint32_t sid) {
        if (_scriptsPanel) {
            _scriptsPanel->selectSpatialScriptRow(sid);
        }
    });
    connect(_currentEditorWidget, &EditorWidget::spatialScriptEditActivated, this,
        [this](uint32_t sid) { openSpatialScriptDialog(sid); });
    connect(_currentEditorWidget, &EditorWidget::mapScriptsChanged, this,
        [this]() { refreshScriptsPanel(); });
    connect(_currentEditorWidget, &EditorWidget::mapEdgeChanged, this,
        [this]() { refreshMapEdgesPanel(); });

    if (_mapInfoPanel) {
        connect(_currentEditorWidget, &EditorWidget::playerPositionSelected,
            this, [this](int hexPosition) {
                if (_mapInfoPanel && _currentEditorWidget) {
                    _mapInfoPanel->setPlayerPosition(hexPosition, _currentEditorWidget->getCurrentElevation());
                }
            });
    }

    applySelectionColorsToEditor();

    spdlog::debug("Connected EditorWidget instance signals");
}

void MainWindow::openSpatialScriptDialog(std::optional<uint32_t> editSid) {
    if (!_currentEditorWidget) {
        return;
    }

    // Editing needs the target script to still exist.
    std::optional<EditorWidget::SpatialScriptInfo> info;
    if (editSid.has_value()) {
        info = _currentEditorWidget->spatialScriptInfo(*editSid);
        if (!info) {
            showStatusMessage("Spatial script no longer exists");
            return;
        }
    }

    // One dialog at a time: re-focus an open one rather than stacking a second.
    if (_spatialScriptDialog) {
        _spatialScriptDialog->raise();
        _spatialScriptDialog->activateWindow();
        return;
    }

    // Non-modal (like the player-position pick) so the map stays clickable for "Pick on map".
    auto* dialog = new SpatialScriptDialog(ScriptSelectorDialog::buildEntries(*_resourcesShared), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    _spatialScriptDialog = dialog;
    _editingSpatialSid = editSid.value_or(MapScript::NONE);

    if (info) {
        dialog->setWindowTitle("Edit Spatial Script");
        dialog->setProgramIndex(static_cast<int>(info->programIndex));
        dialog->setTile(info->tile);
        dialog->setElevation(info->elevation);
        dialog->setRadius(info->radius);
    }

    dialog->setSourceEditRequester([this](int programIndex) {
        if (_scriptSourceService) {
            _scriptSourceService->editScriptSource(programIndex);
        }
    });
    connect(dialog, &SpatialScriptDialog::pickPositionRequested, this, &MainWindow::pickSpatialScriptPosition);
    connect(dialog, &QDialog::accepted, this, &MainWindow::applySpatialScriptDialog);
    connect(dialog, &QObject::destroyed, this, [this]() { _spatialScriptDialog = nullptr; });
    dialog->show();
}

void MainWindow::pickSpatialScriptPosition() {
    if (!_currentEditorWidget || !_spatialScriptDialog) {
        return;
    }
    _spatialScriptDialog->hide(); // step aside; the map is clickable because the dialog is non-modal
    _currentEditorWidget->beginHexPick(
        [this](std::optional<int> hex) {
            if (!_spatialScriptDialog) {
                return; // dialog was closed while picking
            }
            if (hex.has_value() && _currentEditorWidget) {
                _spatialScriptDialog->setTile(*hex);
                // The clicked hex lives on the current elevation, so match it.
                _spatialScriptDialog->setElevation(_currentEditorWidget->getCurrentElevation());
            }
            _spatialScriptDialog->show();
            _spatialScriptDialog->raise();
            _spatialScriptDialog->activateWindow();
        },
        "Click a hex for the spatial-script position (Esc to cancel)");
}

void MainWindow::applySpatialScriptDialog() {
    if (!_currentEditorWidget || !_spatialScriptDialog) {
        return;
    }
    const SpatialScriptDialog* dialog = _spatialScriptDialog;
    if (dialog->programIndex() < 0) {
        return; // OK is disabled without a chosen script, but guard anyway
    }
    if (_editingSpatialSid == MapScript::NONE) {
        _currentEditorWidget->addSpatialScript(static_cast<uint32_t>(dialog->programIndex()),
            dialog->tile(), dialog->elevation(), dialog->radius());
    } else {
        _currentEditorWidget->editSpatialScript(_editingSpatialSid,
            static_cast<uint32_t>(dialog->programIndex()), dialog->tile(), dialog->elevation(), dialog->radius());
    }
}

void MainWindow::refreshScriptsPanel() {
    if (_scriptsPanel && _currentEditorWidget) {
        _scriptsPanel->setMap(_currentEditorWidget->getMap());
        // populate() drops the selection; re-assert the shared spatial selection so the row stays lit.
        _scriptsPanel->selectSpatialScriptRow(_currentEditorWidget->selectedSpatialScript());
    }
}

void MainWindow::refreshMapEdgesPanel() {
    if (!_mapInfoPanel) {
        return;
    }
    if (!_currentEditorWidget) {
        _mapInfoPanel->setMapEdgeState(false, 0, 0, false, { false, false, false, false });
        return;
    }

    const bool hasMap = _currentEditorWidget->getMap() != nullptr;
    const int elevation = _currentEditorWidget->currentElevation();
    const auto& edge = _currentEditorWidget->mapEdge();

    int version = 0;
    int zoneCount = 0;
    std::array<bool, 4> clip{ false, false, false, false };
    if (edge.has_value() && elevation >= 0 && elevation < MapEdge::ELEVATION_COUNT) {
        version = edge->version;
        const auto& elev = edge->elevations[elevation];
        zoneCount = static_cast<int>(elev.zones.size());
        clip = { elev.clipSides.left, elev.clipSides.top, elev.clipSides.right, elev.clipSides.bottom };
    }
    const int selected = _currentEditorWidget->selectedEdgeZone();
    const bool hasSelectedZone = selected >= 0 && selected < zoneCount;

    _mapInfoPanel->setMapEdgeState(hasMap, version, zoneCount, hasSelectedZone, clip);
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
    _currentEditorWidget->setShowSpatialScripts(_showSpatialScriptsAction->isChecked());
    _currentEditorWidget->setShowMapEdges(_showMapEdgesAction->isChecked());
    _currentEditorWidget->setShowUnreachableAreas(_showUnreachableAction->isChecked());
    _currentEditorWidget->setMergeSelectionOutlines(_mergeOutlinesAction->isChecked());
    _currentEditorWidget->setEdgeScrollEnabled(_edgeScrollAction->isChecked());
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
        refreshMapEdgesPanel(); // setMap resets the edge group; re-fill it from the loaded map's .edg
    }

    if (_scriptsPanel) {
        _scriptsPanel->setMap(map);
    }

    // The selection panel needs the live map to show tile info; without this it
    // keeps the (often null) map it was given at connect time and every tile
    // selection falls back to "No tile selected".
    if (_selectionPanel) {
        _selectionPanel->setMap(map);
    }

    updateElevationMenu(map);

    if (_tilePalettePanel && map) {
        try {
            const auto* tileList = _resourcesShared->repository().find<Lst>("art/tiles/tiles.lst");

            if (tileList) {
                _tilePalettePanel->loadTiles(tileList);
                spdlog::debug("Loaded tiles.lst into palette: {} tiles", tileList->list().size());
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
            spdlog::debug("Loaded objects into ObjectPalettePanel");
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
    spdlog::debug("MainWindow: Handling request to load map: {} (filesystem: {})", mapPath, forceFilesystem);

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

    spdlog::debug("Map loading completed from MainWindow");
}

void MainWindow::setupPanelsMenu() {
    struct PanelToggleSpec {
        const char* label;
        QDockWidget* dock;
        QAction** actionRef;
    };

    const std::array<PanelToggleSpec, 6> panelToggleSpecs = { {
        { "Map &Information", _mapInfoDock, &_mapInfoPanelAction },
        { "Scri&pts", _scriptsDock, &_scriptsPanelAction },
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
    // The saved dock state is the user's working layout with a map open. While no map is open the
    // panels are transiently hidden (welcome screen) and _suppressDockStateSave guards programmatic
    // relayout, so in both cases we must not overwrite the saved layout with a transient one.
    if (_suppressDockStateSave || !_currentEditorWidget || !_currentEditorWidget->getMap()) {
        return;
    }

    auto& settings = *_settings;
    const QByteArray state = saveState();
    _restoredDockState = state; // re-shown verbatim by showPanelsForMap() when the next map opens
    settings.setDockState(state);
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
        _restoredDockState = state; // the layout to re-show once a map opens (see showPanelsForMap)

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
        _restoredDockState = saveState(); // no saved layout: remember the default to re-show on map open
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

    const bool previousSuppression = _suppressDockStateSave;
    _suppressDockStateSave = true; // programmatic show/hide is not a user layout change

    if (action) {
        QSignalBlocker blocker(*action);
        action->setChecked(visible);
    }

    dock->setVisible(visible);

    _suppressDockStateSave = previousSuppression;
}

void MainWindow::showPanelsForMap() {
    // Re-apply the persisted dock layout (visibility + geometry) that was hidden for the welcome
    // screen. Suppress saving so re-applying it isn't mistaken for a user change.
    const bool previousSuppression = _suppressDockStateSave;
    _suppressDockStateSave = true;
    if (!_restoredDockState.isEmpty()) {
        restoreState(_restoredDockState);
    }
    _suppressDockStateSave = previousSuppression;

    updatePanelMenuActions();
}

void MainWindow::hidePanelsForNoMap() {
    const bool previousSuppression = _suppressDockStateSave;
    _suppressDockStateSave = true; // hiding for the welcome screen must not overwrite the saved layout

    for (const auto& [dock, action] : managedDockActionPairs()) {
        setDockVisibility(dock, action, dock == _fileBrowserDock);
    }

    if (_fileBrowserDock) {
        _fileBrowserDock->raise();
    }

    _suppressDockStateSave = previousSuppression;
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

    spdlog::debug("File browser panel shown and raised");
}

void MainWindow::closeCurrentMap() {
    if (!_currentEditorWidget) {
        return;
    }

    spdlog::debug("Closing current map");

    stopGameLoop();

    _centralStack->removeWidget(_currentEditorWidget);
    _currentEditorWidget->deleteLater();
    _currentEditorWidget = nullptr;

    _centralStack->setCurrentWidget(_welcomeWidget);

    // No map is open now: clear the dirty flag and reset the title off the closed map, otherwise the
    // title bar keeps the closed map's name (and a stale "*") while the welcome screen is shown.
    _mapModified = false;
    updateWindowTitle();

    // The editor and its Map are gone; clear the panels' borrowed map pointers (Map Info, Scripts,
    // Selection) so they don't dangle before the next map opens.
    updateMapInfo(nullptr);

    hidePanelsForNoMap();

    spdlog::debug("Current map closed successfully");
    updateUndoRedoActions();
    refreshCompleteness();
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

        // Keep the View-menu toggle and the live editor in step with the dialog's edge-scroll choice.
        // Block the action's signal so re-checking it doesn't loop back into another settings save.
        if (_edgeScrollAction && _settings) {
            const QSignalBlocker block(_edgeScrollAction);
            _edgeScrollAction->setChecked(_settings->getEdgeScrollEnabled());
        }
        if (_currentEditorWidget && _settings) {
            _currentEditorWidget->setEdgeScrollEnabled(_settings->getEdgeScrollEnabled());
        }
    });

    int result = dialog.exec();

    if (result == QDialog::Accepted) {
        spdlog::debug("Settings dialog closed with changes");
    } else {
        spdlog::debug("Settings dialog cancelled");
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

void MainWindow::showFillDialog() {
    if (!_currentEditorWidget || !hasActiveMap()) {
        showStatusMessage("Open a map before filling a selection.");
        return;
    }
    if (!_currentEditorWidget->hasFillableSelection()) {
        showStatusMessage("Select an area (floor/roof tiles or hexes) to fill first.");
        return;
    }
    // The dialog drives the editor's preview/apply over the current selection; closing it clears any
    // live ghost (its destructor calls clearFillPreview).
    FillDialog dialog(*_currentEditorWidget, this);
    dialog.exec();
}

void MainWindow::updateFillSelectionAction() {
    if (!_fillSelectionAction) {
        return;
    }
    const bool enabled = _currentEditorWidget && hasActiveMap()
        && _currentEditorWidget->hasFillableSelection();
    _fillSelectionAction->setEnabled(enabled);
}

void MainWindow::showMapBrowserDialog() {
    auto dataPaths = _settings->getDataPaths();
    util::ensureFallbackDataPath(dataPaths, Application::getResourcesPath());
    MapBrowserDialog dialog(*_resourcesShared, std::move(dataPaths), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString mapPath = dialog.selectedMapPath();
    if (!mapPath.isEmpty()) {
        handleMapLoadRequest(mapPath.toStdString(), false);
    }
}

void MainWindow::showCompileScriptDialog() {
    if (_scriptSourceService) {
        _scriptSourceService->compileScript();
    }
}

void MainWindow::showDecompileScriptDialog() {
    if (_scriptSourceService) {
        _scriptSourceService->decompileScript();
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
    if (_exitGridsAction) {
        QSignalBlocker blocker(_exitGridsAction);
        _exitGridsAction->setChecked(false);
        _exitGridsAction->setText("Exit Grids");
    }
}

} // namespace geck
