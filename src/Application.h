#pragma once

#include <memory>
#include <filesystem>

#include <QApplication>
#include <SFML/Graphics/RenderWindow.hpp>

namespace geck {

class MainWindow;
class Settings;
namespace resource {
    class GameResources;
}

class Application {
public:
    inline static const std::filesystem::path RESOURCES_DIR = "resources";

    Application(int argc, char** argv);
    ~Application();

    bool isRunning() const;
    void run();

    // Platform-aware resource path resolution
    static std::filesystem::path getResourcesPath();
    static bool isDefaultResourcesPath(const std::filesystem::path& path);

private:
    void initUI();
    std::string processCommandLineArgs();
    void checkDataConfiguration();
    bool showStartupSettingsDialog();
    bool hasEssentialGameData() const;
    void loadDataPaths();

    std::unique_ptr<QApplication> _qtApp;
    std::shared_ptr<Settings> _settings;
    std::unique_ptr<MainWindow> _mainWindow;
    std::shared_ptr<resource::GameResources> _resources;

    void loadMap(const std::filesystem::path& mapPath);
};

} // namespace geck
