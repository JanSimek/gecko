#pragma once

#include <memory>
#include <filesystem>
#include <atomic>
#include <shared_mutex>

#include <QApplication>
#include <SFML/Graphics/RenderWindow.hpp>
#include "ui/IconsFontAwesome6.h"

namespace geck {

class MainWindow;

class Application {
public:
    inline static const std::filesystem::path RESOURCES_DIR = "resources";
    inline static const std::filesystem::path FONT_DIR = "fonts";
    inline static const std::filesystem::path FONT_MAIN = FONT_DIR / +"SourceSansPro-SemiBold.ttf";
    inline static const std::filesystem::path FONT_ICON = FONT_DIR / +FONT_ICON_FILE_NAME_FAS;

    Application(int argc, char** argv);
    ~Application();

    bool isRunning() const;
    void run();

    // Platform-aware resource path resolution
    static std::filesystem::path getResourcesPath();

private:
    void initUI();
    std::string processCommandLineArgs();
    void checkFirstRun();
    void loadDataPaths();

    std::unique_ptr<QApplication> _qtApp;
    std::unique_ptr<MainWindow> _mainWindow;

    void loadMap(const std::filesystem::path& mapPath);
};

} // namespace geck
