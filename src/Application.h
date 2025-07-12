#pragma once

#include <memory>
#include <stack>
#include <filesystem>
#include <atomic>
#include <shared_mutex>

#include <QApplication>
#include <SFML/Graphics/RenderWindow.hpp>
#include "ui/IconsFontAwesome6.h"

namespace geck {

class StateMachine;
class MainWindow;

struct AppData {
    std::shared_ptr<sf::RenderWindow> window;
    std::shared_ptr<StateMachine> stateMachine;
    MainWindow* mainWindow;
};

class Application {
public:
    inline static const std::filesystem::path RESOURCES_DIR = "resources";
    inline static const std::filesystem::path FONT_DIR = "fonts";
    inline static const std::filesystem::path FONT_MAIN = FONT_DIR / + "SourceSansPro-SemiBold.ttf";
    inline static const std::filesystem::path FONT_ICON = FONT_DIR / + FONT_ICON_FILE_NAME_FAS;

    Application(int argc, char** argv, const std::filesystem::path& resourcePath, const std::filesystem::path& mapPath);
    ~Application();

    bool isRunning() const;

    void run();

private:
    void initUI();

    std::unique_ptr<QApplication> _qtApp;
    std::unique_ptr<MainWindow> _mainWindow;
    std::shared_ptr<StateMachine> _stateMachine;
    std::shared_ptr<AppData> _appData;

    void loadMap(const std::filesystem::path& mapPath);
};

} // namespace geck
