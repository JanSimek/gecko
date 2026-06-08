#pragma once

#include <QObject>
#include <QString>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "../format/map/Map.h"

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
 * @brief Service that launches Fallout 2 with the currently edited map.
 *
 * Extracted from MainWindow. Handles validation, saving the map into the game data
 * directory, modifying ddraw.ini and launching the game (directly or via Steam).
 */
class GameLauncher : public QObject {
    Q_OBJECT

public:
    GameLauncher(resource::GameResources& resources, std::shared_ptr<Settings> settings,
        QWidget* dialogParent, std::function<void(const QString&)> showStatus,
        QObject* parent = nullptr);

    void playGame(const Map::MapFile* mapFile, const std::string& mapFilename);

private:
    bool modifyDdrawIni(const std::filesystem::path& ddrawIniPath, const std::string& mapFilename);
    void launchGame(const std::filesystem::path& gameLocation);
    void launchGameViaSteam(const std::string& appId);

    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;
    QWidget* _dialogParent;
    std::function<void(const QString&)> _showStatus;
};

} // namespace geck
