#pragma once

#include <array>
#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QTimer>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QLabel>
#include <QSettings>
#include <QByteArray>
#include <QMenu>
#include <cstdint>
#include <memory>
#include <optional>
#include <filesystem>
#include <utility>
#include <SFML/Window/Event.hpp>

#include "EditorMode.h"

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QHBoxLayout;
QT_END_NAMESPACE

namespace geck {

namespace resource {
    class GameResources;
}

class Settings;
class GameLauncher;
class ExternalEditorLauncher;
class SFMLWidget;
class EditorWidget;
class LoadingWidget;
class WelcomeWidget;
class SelectionPanel;
class MapInfoPanel;
class ScriptsPanel;
class TilePalettePanel;
class ObjectPalettePanel;
class FileBrowserPanel;
class ScriptConsoleWidget;
class SpatialScriptDialog;
class Map;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(std::shared_ptr<resource::GameResources> resources, std::shared_ptr<Settings> settings, QWidget* parent = nullptr);
    ~MainWindow();

    void setEditorWidget(std::unique_ptr<EditorWidget> editorWidget);

    void updateMapInfo(Map* map);

    void startGameLoop();
    void stopGameLoop();

    void connectToEditorWidget();
    // Push the configured selection highlight colours from Settings into the active editor.
    void applySelectionColorsToEditor();

    // Panel visibility management
    void refreshFileBrowser();
    void showFileBrowserPanel();

    // Map management
    void closeCurrentMap();
    bool hasActiveMap() const;

    // Access to palette panels for drag and drop and tile deselection
    ObjectPalettePanel* getObjectPalettePanel() const { return _objectPalettePanel; }
    TilePalettePanel* getTilePalettePanel() const { return _tilePalettePanel; }
    FileBrowserPanel* getFileBrowserPanel() const { return _fileBrowserPanel; }

    // Bring a palette dock to the front of its tab group (used by the eyedropper so the picked
    // object/tile's palette is visible). No-op if the dock is hidden or floating on its own.
    void raiseObjectPalette();
    void raiseTilePalette();

    resource::GameResources& resources() const { return *_resourcesShared; }

signals:
    void newMapRequested();
    void openMapRequested();
    void saveMapRequested();
    void saveMapAsRequested();
    void closeMapRequested();
    void selectAllRequested();
    void deselectAllRequested();
    void showObjectsToggled(bool enabled);
    void showCrittersToggled(bool enabled);
    void showWallsToggled(bool enabled);
    void showRoofsToggled(bool enabled);
    void showScrollBlockersToggled(bool enabled);
    void showWallBlockersToggled(bool enabled);
    void showHexGridToggled(bool enabled);
    void showLightOverlaysToggled(bool enabled);
    void showExitGridsToggled(bool enabled);
    void showSpatialScriptsToggled(bool enabled);
    void showMapEdgesToggled(bool enabled);
    void elevationChanged(int elevation);
    void selectionModeRequested();
    void rotateObjectRequested();
    void toggleScrollBlockerRectangleMode();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private slots:
    void updateAndRender();
    void handleMapLoadRequest(const std::string& mapPath, bool forceFilesystem = false);
    void updateHexIndexDisplay(int hexIndex);
    void updateModeDisplay(const QString& modeText, const QString& iconPath);
    void showPreferences();
    void showAbout();
    void onPlayGame();
    void showSavePatternDialog();
    void showStampPatternDialog();
    void showFillDialog();
    void showMapBrowserDialog();

public slots:
    void showStatusMessage(const QString& message);
    void clearStatusMessage();
    void deselectMarkExitsMode();

private:
    using DockActionPair = std::pair<QDockWidget*, QAction*>;

    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    // Apply the selection-mode dropdown's layer checkboxes to the editor (combinable floor / roof
    // / objects), clearing any exclusive special mode and refreshing the toolbar button text.
    void applySelectionLayersFromMenu();
    void setupToolModeActions();
    // Activate the Exit-Grids tool in its currently-selected sub-mode (the checked dropdown item),
    // or return to Select when `checked` is false. Updates the toolbar button text.
    void applyExitGridsTool(bool checked);
    void syncToolModeActions(EditorMode mode);
    void setupDockWidgets();
#ifdef GECK_SCRIPTING_ENABLED
    // Connects the script console's Run signal to the editor and adds its View-menu toggle.
    void wireScriptConsole();
#endif
    void setupStatusBar();
    void setupPanelsMenu();
    void connectMenuSignals();
    void updatePanelMenuActions();
    void updateElevationMenu(Map* map);
    void syncMenuStateToEditorWidget();
    void updateUndoRedoActions();
    // Enables "Fill Selection…" only when a map is open and the selection has a fillable layer
    // (floor/roof tiles or hexes). Hooked to selectionChanged and re-seeded on map switch.
    void updateFillSelectionAction();
    /// Sets the window title to the current map's name (plus the modified "[*]" marker).
    void updateWindowTitle();
    /// Records whether the current map has unsaved edits and reflects it in the title bar.
    void setMapModified(bool modified);
    /// If the current map has unsaved edits, prompts Save/Discard/Cancel. Returns true if it is safe
    /// to discard the map (saved or explicitly discarded), false if the user cancelled.
    bool maybeSaveChanges();
    QIcon themedIcon(const QString& iconPath) const;
    // A map just opened: re-apply the persisted dock layout that was transiently hidden for the
    // welcome screen (no map). Restores from the saved Qt dock state, the single source of truth.
    void showPanelsForMap();
    void hidePanelsForNoMap();
    void setDockVisibility(QDockWidget* dock, QAction* action, bool visible);
    QAction* addPanelToggleAction(const QString& label, QDockWidget* dock, QAction*& actionRef);
    std::array<QDockWidget*, 6> managedDocks() const;
    std::array<DockActionPair, 6> managedDockActionPairs() const;
    void applyDefaultDockPlacements();
    void applyDefaultPanelDockLayout();
    void connectFileBrowserSignals();
    void connectPanelSignals();
    // Open the (non-modal) Spatial Script dialog: editSid empty = add a new script, set = edit that
    // one pre-filled. On accept it applies an undoable add/edit. Shared by the Add button, the
    // Scripts-panel edit request, and the map double-click.
    void openSpatialScriptDialog(std::optional<uint32_t> editSid);
    // "Pick on map" from the open dialog: hide it, let the user click a hex (beginHexPick), then feed
    // the hex + its elevation back and re-show the dialog.
    void pickSpatialScriptPosition();
    // Apply the open dialog's values (add or edit, per _editingSpatialSid).
    void applySpatialScriptDialog();
    // Rebuild the Scripts panel from the current map and re-assert the shared spatial selection.
    // Called after a spatial-script add/edit/delete and after undo/redo.
    void refreshScriptsPanel();
    void replaceDockPanelWidget(QDockWidget* dock, QWidget* panel, QSizePolicy::Policy verticalPolicy);
    void rebuildResourcePanels();
    void rebuildGameResourcesFromSettings();

    // The writable data path's maps/ folder (created if needed), used as the default save target; empty
    // when no writable Data Path is configured.
    std::filesystem::path writableMapsDir() const;
    // Post-save housekeeping shared by Save / Save As: flush map-name edits, clear the modified flag, and
    // refresh the title (which also picks up a Save As rename).
    void handleMapSaved();

    // Dock widget state management
    void saveDockWidgetState();
    void restoreDockWidgetState();
    void resetDockWidgetLayout();
    void restoreDefaultLayout();

    void convertQtEventToSFML(QKeyEvent* qtEvent, sf::Event& sfmlEvent, bool pressed);

    QStackedWidget* _centralStack;
    QTimer* _gameLoopTimer;
    std::shared_ptr<resource::GameResources> _resourcesShared;
    std::shared_ptr<Settings> _settings;
    std::unique_ptr<GameLauncher> _gameLauncher;
    std::unique_ptr<ExternalEditorLauncher> _externalEditorLauncher;

    // Current widgets
    EditorWidget* _currentEditorWidget;
    WelcomeWidget* _welcomeWidget;

    // Menu items
    QMenuBar* _menuBar;
    QMenu* _fileMenu;
    QMenu* _editMenu;
    QMenu* _viewMenu;
    QMenu* _panelsMenu;
    QMenu* _elevationMenu;
    QMenu* _helpMenu;

    // Toolbar
    QToolBar* _mainToolBar;
    QAction* _selectToolAction = nullptr;
    // Unified Exit-Grids tool: one checkable toolbar button plus a dropdown that picks the
    // sub-mode. Toggling the button on activates the chosen sub-mode; off returns to Select.
    QAction* _exitGridsAction = nullptr;
    QMenu* _exitGridsMenu = nullptr;
    QAction* _exitGridPlaceHexAction = nullptr;   // "Place single hex" -> EditorMode::PlaceExitGrid
    QAction* _exitGridDrawRegionAction = nullptr; // "Draw edge"       -> EditorMode::MarkExits
    QAction* _rotateAction = nullptr;
    QAction* _fillSelectionAction = nullptr;
    QAction* _undoAction = nullptr;
    QAction* _redoAction = nullptr;

    // Status bar
    QStatusBar* _statusBar;
    QLabel* _statusLabel;
    // Permanent contextual key-hint, kept separate from _statusLabel so transient
    // messages (_showStatus) never overwrite it. Driven by EditorWidget::hintChanged.
    QLabel* _hintLabel;
    QLabel* _hexIndexLabel;

    // Dock widgets for panels
    QDockWidget* _mapInfoDock;
    QDockWidget* _scriptsDock;
    QDockWidget* _selectionDock;
    QDockWidget* _tilePaletteDock;
    QDockWidget* _objectPaletteDock;
    QDockWidget* _fileBrowserDock;
#ifdef GECK_SCRIPTING_ENABLED
    QDockWidget* _scriptConsoleDock = nullptr;
    ScriptConsoleWidget* _scriptConsole = nullptr;
#endif
    // Guards the persisted dock layout while docks are re-laid-out programmatically (hidden for the
    // welcome screen, re-shown for a map), so those transient changes aren't written back as if the
    // user made them.
    bool _suppressDockStateSave = false;
    // The dock layout (visibility + geometry) to re-apply when a map opens; seeded at startup from the
    // saved state and refreshed on every genuine user change. See saveDockWidgetState/showPanelsForMap.
    QByteArray _restoredDockState;
    bool _mapModified = false; // current map has edits not yet written to disk

    // Panel widgets
    SelectionPanel* _selectionPanel;
    MapInfoPanel* _mapInfoPanel;
    ScriptsPanel* _scriptsPanel;

    // The open non-modal Spatial Script dialog (add or edit), or null. _editingSpatialSid is the SID
    // being edited, or MapScript::NONE (0xFFFFFFFF) when adding.
    SpatialScriptDialog* _spatialScriptDialog = nullptr;
    uint32_t _editingSpatialSid = 0xFFFFFFFFu;
    TilePalettePanel* _tilePalettePanel;
    ObjectPalettePanel* _objectPalettePanel;
    FileBrowserPanel* _fileBrowserPanel;

    // Panel menu actions
    QAction* _mapInfoPanelAction;
    QAction* _scriptsPanelAction;
    QAction* _selectionPanelAction;
    QAction* _tilePalettePanelAction;
    QAction* _objectPalettePanelAction;
    QAction* _fileBrowserPanelAction;

    // Toolbar actions
    QAction* _selectionModeAction;
    QMenu* _selectionModeMenu;
    // Combinable layer checkboxes in the selection-mode dropdown (floor / roof / objects).
    QAction* _floorLayerAction = nullptr;
    QAction* _roofLayerAction = nullptr;
    QAction* _objectsLayerAction = nullptr;

    // Elevation menu actions
    QAction* _elevation1Action;
    QAction* _elevation2Action;
    QAction* _elevation3Action;

    // View menu actions for visibility toggles
    QAction* _showObjectsAction;
    QAction* _showCrittersAction;
    QAction* _showWallsAction;
    QAction* _showRoofsAction;
    QAction* _showScrollBlockersAction;
    QAction* _showWallBlockersAction;
    QAction* _showHexGridAction;
    QAction* _showLightOverlaysAction;
    QAction* _showExitGridsAction;
    QAction* _showSpatialScriptsAction;
    QAction* _showMapEdgesAction;
    QAction* _mergeOutlinesAction;
    QAction* _edgeScrollAction;

    bool _isRunning;
};

} // namespace geck
