#pragma once

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
#include <memory>
#include <filesystem>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QHBoxLayout;
QT_END_NAMESPACE

namespace sf {
class RenderWindow;
class Event;
}

namespace geck {

class SFMLWidget;
class EditorWidget;
class LoadingWidget;
class WelcomeWidget;
class SelectionPanel;
class MapInfoPanel;
class TilePalettePanel;
class ObjectPalettePanel;
class FileBrowserPanel;
class Map;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void setEditorWidget(std::unique_ptr<EditorWidget> editorWidget);

    void updateMapInfo(Map* map);

    void startGameLoop();
    void stopGameLoop();

    void connectToEditorWidget();
    
    // Panel visibility management
    void showAllPanels();
    void hideNonEssentialPanels();
    void refreshFileBrowser();
    void showFileBrowserPanel();
    
    // Access to palette panels for drag and drop and tile deselection
    ObjectPalettePanel* getObjectPalettePanel() const { return _objectPalettePanel; }
    TilePalettePanel* getTilePalettePanel() const { return _tilePalettePanel; }
    FileBrowserPanel* getFileBrowserPanel() const { return _fileBrowserPanel; }
    
    // PRO editor functionality
    bool openProEditorForSelectedObject();

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
    void onPlayGame();

public slots:
    void showStatusMessage(const QString& message);
    void clearStatusMessage();

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupDockWidgets();
    void setupStatusBar();
    void setupPanelsMenu();
    void connectMenuSignals();
    void updatePanelMenuActions();
    void updateElevationMenu(Map* map);
    
    // Text file handling
    bool isTextFile(const QString& filePath) const;
    void openTextFileWithEditor(const QString& vfsFilePath);

    // Dock widget state management
    void saveDockWidgetState();
    void restoreDockWidgetState();
    void resetDockWidgetLayout();
    void restoreDefaultLayout();

    void convertQtEventToSFML(QKeyEvent* qtEvent, sf::Event& sfmlEvent, bool pressed);
    
    // Play game helper methods
    bool modifyDdrawIni(const std::filesystem::path& ddrawIniPath, const std::string& mapFilename);
    void launchGame(const std::filesystem::path& gameLocation);
    void launchGameViaSteam(const std::string& appId);

    QStackedWidget* _centralStack;
    QTimer* _gameLoopTimer;

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

    // Toolbar
    QToolBar* _mainToolBar;

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
    QAction* _modeAction;

    // Elevation menu actions
    QAction* _elevation1Action;
    QAction* _elevation2Action;
    QAction* _elevation3Action;

    bool _isRunning;
};

} // namespace geck