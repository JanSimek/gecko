#pragma once

#include <QObject>
#include <QString>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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
 * @brief How the editor's data paths map onto the game's mod load order.
 *
 * fallout2-ce mounts every entry of `mods/mods_order.txt` through `dbOpen`, which accepts a DAT
 * archive or a plain directory, ranking them above patchXXX.dat/ce.dat/master.dat but below
 * master_patches. Entries resolve as `mods/<entry>` relative to the working directory, so a
 * relative entry reaches any editor data path on the same volume without copying anything.
 */
struct EditorDataMountPlan {
    /// Entries to write, in mount order - the engine prepends each mount, so the last one wins.
    std::vector<std::string> modsOrderEntries;
    /// Data paths that cannot be expressed as an entry, e.g. on another Windows volume.
    std::vector<std::filesystem::path> unmountable;
    /// Data paths naming an archive the game already loads from its own folder, skipped so a second
    /// copy of it cannot outrank the player's mods.
    std::vector<std::filesystem::path> alreadyLoadedByGame;
};

/**
 * @brief Works out which editor data paths have to be mounted into the game and how (pure).
 *
 * Paths already inside @p gameDataDirectory are skipped: the game reads those anyway through
 * master_patches, its own DATs or the existing mod list.
 *
 * @param archivesInGameFolder lower-case names of the files sitting directly in the game folder,
 *        which is where the engine picks up master.dat, critter.dat, f2_res.dat, ce.dat and the
 *        patchXXX chain by itself. An editor data path naming one of them is a second copy of base
 *        game data and must not be mounted, or it would outrank every mod the player has installed.
 *        Deliberately a lookup rather than a fixed name list: a mod archive of the editor's own,
 *        even one called patch001.dat, is mounted as long as the game has no file of that name.
 */
EditorDataMountPlan planEditorDataMounts(const std::filesystem::path& gameDataDirectory,
    const std::vector<std::filesystem::path>& editorDataPaths,
    const std::vector<std::string>& archivesInGameFolder);

/**
 * @brief Replaces the editor-managed block of a mods_order.txt with @p entries (pure).
 *
 * Lines outside the block - the player's own mod order - are preserved verbatim, and the block is
 * appended last so editor data outranks it. The markers contain a semicolon, which the engine's
 * parser treats as a comment.
 */
std::string applyManagedModsOrderBlock(const std::string& existingContent, const std::vector<std::string>& entries);

/**
 * @brief Reports configuration problems that would make the game load data the editor is not
 * editing (pure).
 *
 * @param gameDataDirectory        directory the game is launched against
 * @param executableDirectory      directory holding the game executable
 * @param executableDirectoryHasGameData whether @p executableDirectory looks like its own Fallout 2
 *                                 installation (caller supplies this so the check stays testable)
 * @param unmountableDataPaths     editor data paths that could not be mounted into the game
 * @return one human-readable sentence per problem; empty when the configuration is coherent
 */
std::vector<std::string> collectLaunchConfigurationWarnings(
    const std::filesystem::path& gameDataDirectory,
    const std::filesystem::path& executableDirectory,
    bool executableDirectoryHasGameData,
    const std::vector<std::filesystem::path>& unmountableDataPaths);

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

    /// Restores mods_order.txt if the game is still running when the editor shuts down.
    ~GameLauncher() override;

    void playGame(const Map::MapFile* mapFile, const std::string& mapFilename);

private:
    bool confirmLaunchConfiguration(const std::filesystem::path& gameDataDirectory,
        const std::filesystem::path& executablePath,
        const std::vector<std::filesystem::path>& unmountableDataPaths) const;
    bool modifyDdrawIni(const std::filesystem::path& ddrawIniPath, const std::string& mapFilename) const;
    bool writeContentConfigPatch(const std::filesystem::path& gameDataDirectory, const std::string& mapFilename) const;
    bool writeModsOrder(const std::filesystem::path& gameDataDirectory, const std::vector<std::string>& entries);
    /// noexcept so the destructor can call it without a catch of its own: an escaping exception
    /// while unwinding is worse than the failed restore, which the next launch repairs anyway.
    void restoreModsOrder() noexcept;
    void launchGame(const std::filesystem::path& executablePath, const std::filesystem::path& workingDirectory);

    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;
    QWidget* _dialogParent;
    std::function<void(const QString&)> _showStatus;

    /// mods_order.txt as it was before the editor added its block, restored once the game exits.
    /// Empty optional inside means the file did not exist and has to be removed again.
    std::filesystem::path _modsOrderPath;
    std::optional<std::string> _modsOrderOriginal;
    bool _modsOrderPatched = false;
};

} // namespace geck
