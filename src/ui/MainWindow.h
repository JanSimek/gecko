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
#include <memory>

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
class SelectionPanel;
class MapInfoPanel;
class TilePalettePanel;
class Map;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void setEditorWidget(std::unique_ptr<EditorWidget> editorWidget);
    void setLoadingWidget(std::unique_ptr<LoadingWidget> loadingWidget);
    
    void updateMapInfo(Map* map);

    void startGameLoop();
    void stopGameLoop();
    
    void connectToEditorWidget();

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
    void showHexGridToggled(bool enabled);
    void elevationChanged(int elevation);
    void selectionModeRequested();
    void rotateObjectRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private slots:
    void updateAndRender();
    void handleMapLoadRequest(const std::string& mapPath);
    void updateHexIndexDisplay(int hexIndex);

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupDockWidgets();
    void setupStatusBar();
    void setupPanelsMenu();
    void updatePanelMenuActions();
    
    void convertQtEventToSFML(QKeyEvent* qtEvent, sf::Event& sfmlEvent, bool pressed);

    QStackedWidget* _centralStack;
    QTimer* _gameLoopTimer;
    
    // Current widgets
    EditorWidget* _currentEditorWidget;
    LoadingWidget* _currentLoadingWidget;
    
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
    QLabel* _hexIndexLabel;
    
    // Dock widgets for panels
    QDockWidget* _mapInfoDock;
    QDockWidget* _selectionDock;
    QDockWidget* _tilePaletteDock;
    
    // Panel widgets
    SelectionPanel* _selectionPanel;
    MapInfoPanel* _mapInfoPanel;
    TilePalettePanel* _tilePalettePanel;
    
    // Panel menu actions
    QAction* _mapInfoPanelAction;
    QAction* _selectionPanelAction;
    QAction* _tilePalettePanelAction;
    
    bool _isRunning;
};

} // namespace geck