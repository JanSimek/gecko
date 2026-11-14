#pragma once

#include <memory>
#include <filesystem>
#include <optional>

#include <QApplication>
#include <SFML/Graphics/RenderWindow.hpp>

namespace geck {

class MainWindow;
class Settings;
class LogModel;
class LogModelSink;
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
    // Log-panel record store + the spdlog sink feeding it. Declared before _mainWindow so the
    // model outlives the window's Log panel; the sink is detached in ~Application before either
    // goes away.
    std::unique_ptr<LogModel> _logModel;
    std::shared_ptr<LogModelSink> _logSink;
    std::unique_ptr<MainWindow> _mainWindow;
    std::shared_ptr<resource::GameResources> _resources;
    // Set only when --data/-d was actually passed on the command line (not merely its default
    // value). Used as a reliable fallback when the settings file's own data paths turn out to be
    // the known placeholder-only pattern - a confirmed, persistent bug on at least one launch
    // path where the settings read is permanently stuck on a stale snapshot no in-app fix can
    // correct, so a fresh, trustworthy source independent of that file is the only way through.
    std::optional<std::filesystem::path> _cliDataPathOverride;

    void loadMap(const std::filesystem::path& mapPath);
};

} // namespace geck
