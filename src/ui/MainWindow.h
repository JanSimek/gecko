#pragma once

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QTimer>
#include <QKeyEvent>
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
class StateMachine;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void setStateMachine(std::shared_ptr<StateMachine> stateMachine);
    SFMLWidget* getSFMLWidget() const { return _sfmlWidget; }

    void startGameLoop();
    void stopGameLoop();
    
    void connectToEditorState();

signals:
    void newMapRequested();
    void openMapRequested();
    void saveMapRequested();
    void showObjectsToggled(bool enabled);
    void showCrittersToggled(bool enabled);
    void showWallsToggled(bool enabled);
    void showRoofsToggled(bool enabled);
    void showScrollBlockersToggled(bool enabled);
    void elevationChanged(int elevation);
    void selectionModeRequested();
    void rotateObjectRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private slots:
    void updateAndRender();

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupDockWidgets();
    
    void convertQtEventToSFML(QKeyEvent* qtEvent, sf::Event& sfmlEvent, bool pressed);

    SFMLWidget* _sfmlWidget;
    QTimer* _gameLoopTimer;
    std::shared_ptr<StateMachine> _stateMachine;
    
    // Menu items
    QMenuBar* _menuBar;
    QMenu* _fileMenu;
    QMenu* _viewMenu;
    QMenu* _elevationMenu;
    
    // Toolbar
    QToolBar* _mainToolBar;
    
    // Dock widgets for panels
    QDockWidget* _mapInfoDock;
    QDockWidget* _selectedObjectDock;
    QDockWidget* _tileSelectionDock;
    
    bool _isRunning;
};

} // namespace geck