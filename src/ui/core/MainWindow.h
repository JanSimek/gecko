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
#include <QMenu>
#include <memory>
#include <filesystem>
#include <unordered_map>
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
class TilePalettePanel;
class ObjectPalettePanel;
class FileBrowserPanel;
class ScriptConsoleWidget;
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
    void hideNonEssentialPanels();
    void refreshFileBrowser();
    void showFileBrowserPanel();

    // Map management
    void closeCurrentMap();
    bool hasActiveMap() const;

    // Access to palette panels for drag and drop and tile deselection
    ObjectPalettePanel* getObjectPalettePanel() const { return _objectPalettePanel; }
    TilePalettePanel* getTilePalettePanel() const { return _tilePalettePanel; }
    FileBrowserPanel* getFileBrowserPanel() const { return _fileBrowserPanel; }

    resource::GameResources& resources() const { return *_resourcesShared; }

signals:
    void newMapRequested();
    void openMapRequested();
    void saveMapRequested();
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
    void snapshotPanelVisibility();
    void updateUndoRedoActions();
    /// Sets the window title to the current map's name (plus the modified "[*]" marker).
    void updateWindowTitle();
    /// Records whether the current map has unsaved edits and reflects it in the title bar.
    void setMapModified(bool modified);
    /// If the current map has unsaved edits, prompts Save/Discard/Cancel. Returns true if it is safe
    /// to discard the map (saved or explicitly discarded), false if the user cancelled.
    bool maybeSaveChanges();
    QIcon themedIcon(const QString& iconPath) const;
    void restorePanelVisibilitySnapshot();
    void hidePanelsForNoMap();
    void setDockVisibility(QDockWidget* dock, QAction* action, bool visible);
    void persistPanelPreference(QDockWidget* dock, bool visible);
    QAction* addPanelToggleAction(const QString& label, QDockWidget* dock, QAction*& actionRef);
    std::array<QDockWidget*, 5> managedDocks() const;
    std::array<DockActionPair, 5> managedDockActionPairs() const;
    void applyDefaultDockPlacements();
    void applyDefaultPanelDockLayout();
    void connectFileBrowserSignals();
    void connectPanelSignals();
    void replaceDockPanelWidget(QDockWidget* dock, QWidget* panel, QSizePolicy::Policy verticalPolicy);
    void rebuildResourcePanels();
    void rebuildGameResourcesFromSettings();

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
    QAction* _markExitsAction = nullptr;
    QAction* _placeExitGridAction = nullptr;
    QAction* _rotateAction = nullptr;
    QAction* _undoAction = nullptr;
    QAction* _redoAction = nullptr;

    // Status bar
    QStatusBar* _statusBar;
    QLabel* _statusLabel;
    QLabel* _hexIndexLabel;

    // Dock widgets for panels
    QDockWidget* _mapInfoDock;
    QDockWidget* _selectionDock;
    QDockWidget* _tilePaletteDock;
    QDockWidget* _objectPaletteDock;
    QDockWidget* _fileBrowserDock;
#ifdef GECK_SCRIPTING_ENABLED
    QDockWidget* _scriptConsoleDock = nullptr;
    ScriptConsoleWidget* _scriptConsole = nullptr;
#endif
    std::unordered_map<QDockWidget*, bool> _panelVisibilitySnapshot;
    bool _suppressPanelSnapshotUpdates = false;
    bool _suppressPanelPreferenceUpdates = false;
    bool _mapModified = false; // current map has edits not yet written to disk

    // Panel widgets
    SelectionPanel* _selectionPanel;
    MapInfoPanel* _mapInfoPanel;
    TilePalettePanel* _tilePalettePanel;
    ObjectPalettePanel* _objectPalettePanel;
    FileBrowserPanel* _fileBrowserPanel;

    // Panel menu actions
    QAction* _mapInfoPanelAction;
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
    QAction* _mergeOutlinesAction;

    bool _isRunning;
};

} // namespace geck
