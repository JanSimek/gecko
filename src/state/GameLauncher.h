#pragma once

#include <QObject>
#include <QString>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "format/map/Map.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace geck {

namespace resource {
    class GameResources;
}

class Settings;

/**
 * @brief Applies the StartingMap setting to ddraw.ini content (pure transformation).
 *
 * Takes the full ddraw.ini file contents and returns the modified contents with the
 * StartingMap entry inside the [Misc] section set to @p mapFilename. The section and/or
 * key are created if missing. This is the in-memory core of GameLauncher::modifyDdrawIni
 * and is exposed as a free function so it can be unit tested without filesystem access.
 */
std::string applyStartingMapToDdrawIni(const std::string& iniContent, const std::string& mapFilename);

/**
 * @brief Applies the starting map to fallout2-ce's content config (pure transformation).
 *
 * Sets `map` inside the `[start]` section, creating the section and/or key when missing.
 * fallout2-ce only reads ddraw.ini's StartingMap once, while migrating sfall settings into
 * `data/config/game#patch.cfg`, and skips that migration as soon as the file exists. Writing
 * the key ourselves is therefore the only way to change the starting map on every launch.
 */
std::string applyStartingMapToContentConfig(const std::string& configContent, const std::string& mapFilename);

/**
 * @brief Reports configuration problems that would make the game load data the editor is not
 * editing (pure).
 *
 * @param gameDataDirectory        directory the game is launched against
 * @param executableDirectory      directory holding the game executable
 * @param executableDirectoryHasGameData whether @p executableDirectory looks like its own Fallout 2
 *                                 installation (caller supplies this so the check stays testable)
 * @param editorDataPaths          data paths the editor reads resources from
 * @return one human-readable sentence per problem; empty when the configuration is coherent
 */
std::vector<std::string> collectLaunchConfigurationWarnings(
    const std::filesystem::path& gameDataDirectory,
    const std::filesystem::path& executableDirectory,
    bool executableDirectoryHasGameData,
    const std::vector<std::filesystem::path>& editorDataPaths);

/**
 * @brief Service that launches Fallout 2 with the currently edited map.
 *
 * Extracted from MainWindow. Handles validation, saving the map into the game data
 * directory, pointing the game at the map and launching the game executable.
 */
class GameLauncher : public QObject {
    Q_OBJECT

public:
    GameLauncher(resource::GameResources& resources, std::shared_ptr<Settings> settings,
        QWidget* dialogParent, std::function<void(const QString&)> showStatus,
        QObject* parent = nullptr);

    void playGame(const Map::MapFile* mapFile, const std::string& mapFilename);

private:
    bool confirmLaunchConfiguration(const std::filesystem::path& gameDataDirectory,
        const std::filesystem::path& executablePath);
    bool modifyDdrawIni(const std::filesystem::path& ddrawIniPath, const std::string& mapFilename);
    bool writeContentConfigPatch(const std::filesystem::path& gameDataDirectory, const std::string& mapFilename);
    void launchGame(const std::filesystem::path& executablePath, const std::filesystem::path& workingDirectory);

    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;
    QWidget* _dialogParent;
    std::function<void(const QString&)> _showStatus;
};

} // namespace geck
