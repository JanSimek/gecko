#include "GameLauncher.h"

#include "resource/GameResources.h"
#include "state/MapSaveService.h"
#include "ui/Settings.h"
#include "ui/QtDialogs.h"
#include "util/GameDataPathResolver.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

#include <QProcess>
#include <QStringList>
#include <spdlog/spdlog.h>

namespace geck {

namespace {

    /**
     * Set `key=value` inside @p sectionHeader (e.g. "[Misc]"), creating the section and/or the key
     * when missing and leaving every other line untouched. A commented-out `;key=` is activated.
     *
     * Line terminators are preserved per line: ddraw.ini ships CRLF in a real installation, and a
     * trailing CR would otherwise defeat the section-header match.
     */
    std::string applyIniSetting(const std::string& iniContent, const std::string& sectionHeader,
        const std::string& key, const std::string& value) {
        const std::string assignment = key + "=";
        const std::string commentedAssignment = ";" + assignment;

        std::string content;
        std::string addedLineEnding = "\n";
        bool lineEndingKnown = false;
        bool inTargetSection = false;
        bool sectionFound = false;
        bool keyFound = false;

        std::istringstream stream(iniContent);
        std::string line;
        while (std::getline(stream, line)) {
            const bool crlf = line.ends_with("\r");
            if (crlf) {
                line.pop_back();
            }
            if (!lineEndingKnown) {
                addedLineEnding = crlf ? "\r\n" : "\n";
                lineEndingKnown = true;
            }

            if (line.starts_with("[") && line.ends_with("]")) {
                inTargetSection = line == sectionHeader;
                sectionFound = sectionFound || inTargetSection;
            } else if (inTargetSection && (line.starts_with(assignment) || line.starts_with(commentedAssignment))) {
                line = assignment + value;
                keyFound = true;
            }

            content += line + (crlf ? "\r\n" : "\n");
        }

        if (keyFound) {
            return content;
        }

        if (!sectionFound) {
            return content + addedLineEnding + sectionHeader + addedLineEnding + assignment + value + addedLineEnding;
        }

        // The section exists but holds no such key: insert it directly below the section header.
        const size_t headerPos = content.find(sectionHeader);
        const size_t headerLineEnd = content.find('\n', headerPos);
        content.insert(headerLineEnd + 1, assignment + value + addedLineEnding);
        return content;
    }

    /** Read @p path (a missing file counts as empty), run @p transform over it and write the result back. */
    template <typename Transform>
    bool patchConfigFile(const std::filesystem::path& path, Transform&& transform) {
        std::error_code ec;
        std::string fileContent;
        if (std::filesystem::exists(path, ec)) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                spdlog::error("Failed to open {} for reading", path.string());
                return false;
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            fileContent = buffer.str();
        } else if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::ofstream outFile(path, std::ios::binary);
        if (!outFile.is_open()) {
            spdlog::error("Failed to open {} for writing", path.string());
            return false;
        }

        outFile << transform(fileContent);
        return outFile.good();
    }

    /// Markers around the editor-managed part of mods_order.txt. Both carry a semicolon, which the
    /// engine's mod-list parser treats as a comment, so it never tries to mount them.
    constexpr std::string_view kManagedBlockBegin = "; gecko: editor data paths - regenerated on every Play";
    constexpr std::string_view kManagedBlockEnd = "; gecko: end of editor data paths";

    /** Write @p content to @p path verbatim, creating parent directories as needed. */
    bool writeFileVerbatim(const std::filesystem::path& path, const std::string& content) {
        if (!path.parent_path().empty()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::ofstream outFile(path, std::ios::binary);
        if (!outFile.is_open()) {
            spdlog::error("Failed to open {} for writing", path.string());
            return false;
        }

        outFile << content;
        return outFile.good();
    }

    /** Read @p path as raw bytes, or nullopt when it does not exist or cannot be read. */
    std::optional<std::string> readFileVerbatim(const std::filesystem::path& path) {
        if (std::error_code ec; !std::filesystem::exists(path, ec)) {
            return std::nullopt;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            spdlog::error("Failed to open {} for reading", path.string());
            return std::nullopt;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    /** Trim ASCII whitespace, so marker lines match regardless of indentation or a trailing CR. */
    std::string_view trimmed(std::string_view line) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        return line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
    }

    /** Strip a trailing separator so directory paths compare element by element. */
    std::filesystem::path normalizedForCompare(const std::filesystem::path& path) {
        std::filesystem::path normalized = path.lexically_normal();
        if (!normalized.has_filename() && normalized.has_parent_path()) {
            normalized = normalized.parent_path();
        }
        return normalized;
    }

    /** Whether @p candidate is @p root or lives underneath it. */
    bool isSameOrInside(const std::filesystem::path& candidate, const std::filesystem::path& root) {
        const std::filesystem::path normalizedCandidate = normalizedForCompare(candidate);
        const std::filesystem::path normalizedRoot = normalizedForCompare(root);
        if (normalizedCandidate.empty() || normalizedRoot.empty()) {
            return false;
        }

        auto candidatePart = normalizedCandidate.begin();
        for (auto rootPart = normalizedRoot.begin(); rootPart != normalizedRoot.end(); ++rootPart, ++candidatePart) {
            if (candidatePart == normalizedCandidate.end() || *candidatePart != *rootPart) {
                return false;
            }
        }
        return true;
    }

    /**
     * Resolve the binary to start. A macOS .app bundle has to be started through the binary inside
     * Contents/MacOS: launching the bundle via `open` hands it to LaunchServices, which does not
     * inherit our working directory - and fallout2-ce resolves master_patches/critter_patches
     * against exactly that. Returns an empty path when no binary can be found.
     */
    std::filesystem::path resolveLaunchBinary(const std::filesystem::path& executablePath) {
        if (executablePath.extension() != ".app") {
            return executablePath;
        }

        std::error_code ec;
        const std::filesystem::path macosDir = executablePath / "Contents" / "MacOS";
        if (const std::filesystem::path named = macosDir / executablePath.stem(); std::filesystem::exists(named, ec)) {
            return named;
        }

        for (const auto& entry : std::filesystem::directory_iterator(macosDir, ec)) {
            if (entry.is_regular_file(ec)) {
                return entry.path();
            }
        }

        return {};
    }

} // namespace

GameLauncher::GameLauncher(resource::GameResources& resources, std::shared_ptr<Settings> settings,
    QWidget* dialogParent, std::function<void(const QString&)> showStatus, QObject* parent)
    : QObject(parent)
    , _resources(resources)
    , _settings(std::move(settings))
    , _dialogParent(dialogParent)
    , _showStatus(std::move(showStatus)) {
}

GameLauncher::~GameLauncher() {
    try {
        restoreModsOrder();
    } catch (...) {
        // Never propagate out of a destructor. The mod list stays patched; the next launch rewrites
        // it and the block is stripped rather than kept.
    }
}

void GameLauncher::playGame(const Map::MapFile* mapFile, const std::string& mapFilename) {
    auto& settings = *_settings;

    if (!settings.isGameLocationValid()) {
        QtDialogs::showWarning(_dialogParent, "Game Location Not Configured",
            "Fallout 2 game location is not configured or invalid.\n\n"
            "Please set up the game location in Preferences (File > Preferences > Game Location).");
        return;
    }

    if (!mapFile) {
        QtDialogs::showWarning(_dialogParent, "No Map Loaded",
            "No map is currently loaded. Please open or create a map before playing.");
        return;
    }

    std::filesystem::path gameDataDir = settings.getGameLocation(); // Returns data directory for executable installs
    if (gameDataDir.empty()) {
        QtDialogs::showError(_dialogParent, "Play Failed",
            "No game data directory configured. Please set the game data directory in Preferences.");
        return;
    }

    std::filesystem::path executableLocation = settings.getExecutableGameLocation();
    if (executableLocation.empty()) {
        QtDialogs::showError(_dialogParent, "Play Failed",
            "No game executable configured. Please set the game executable in Preferences.");
        return;
    }

    const EditorDataMountPlan mountPlan = planEditorDataMounts(gameDataDir, settings.getDataPaths());
    if (!confirmLaunchConfiguration(gameDataDir, executableLocation, mountPlan.unmountable)) {
        return;
    }

    std::filesystem::path mapsDir = gameDataDir / "data" / "maps";
    std::filesystem::path mapDestination = mapsDir / mapFilename;

    _showStatus(QString("Playing map: %1").arg(QString::fromStdString(mapFilename)));

    try {
        // 1. Save the current map to the game directory
        if (!std::filesystem::exists(mapsDir)) {
            std::filesystem::create_directories(mapsDir);
        }

        const auto bytesWritten = saveMapToFile(_resources, *mapFile, mapDestination);
        if (!bytesWritten.has_value()) {
            QtDialogs::showError(_dialogParent, "Save Failed",
                QString("Failed to save map to game directory: %1").arg(QString::fromStdString(mapDestination.string())));
            return;
        }

        spdlog::debug("Saved map to game directory: {} ({} bytes)", mapDestination.string(), *bytesWritten);

        // 2. Point the game at the map. ddraw.ini is what the original executable plus sfall reads;
        // fallout2-ce only consults it while migrating sfall settings into data/config/game#patch.cfg
        // and skips that migration once the file exists, so write the migrated key ourselves too.
        QStringList unwritten;
        if (!modifyDdrawIni(gameDataDir / "ddraw.ini", mapFilename)) {
            unwritten << "ddraw.ini";
        }
        if (!writeContentConfigPatch(gameDataDir, mapFilename)) {
            unwritten << "data/config/game#patch.cfg";
        }
        // Mount the editor's data paths for this run only; restored once the game exits.
        if (!writeModsOrder(gameDataDir, mountPlan.modsOrderEntries)) {
            unwritten << "mods/mods_order.txt";
        }
        if (!unwritten.isEmpty()) {
            QtDialogs::showWarning(_dialogParent, "Configuration Warning",
                QString("Map saved successfully, but the starting map could not be written to %1. "
                        "You may need to set the starting map manually.")
                    .arg(unwritten.join(" and ")));
        }

        // 3. Launch the game from the data directory: fallout2-ce resolves master_patches and
        // critter_patches against the working directory, so it has to be the tree we just wrote to.
        launchGame(executableLocation, gameDataDir);

    } catch (const std::exception& e) {
        QtDialogs::showError(_dialogParent, "Play Failed",
            QString("Failed to play map: %1").arg(e.what()));
        spdlog::error("Failed to play map: {}", e.what());
    }
}

std::string applyStartingMapToDdrawIni(const std::string& iniContent, const std::string& mapFilename) {
    return applyIniSetting(iniContent, "[Misc]", "StartingMap", mapFilename);
}

std::string applyStartingMapToContentConfig(const std::string& configContent, const std::string& mapFilename) {
    return applyIniSetting(configContent, "[start]", "map", mapFilename);
}

EditorDataMountPlan planEditorDataMounts(const std::filesystem::path& gameDataDirectory,
    const std::vector<std::filesystem::path>& editorDataPaths) {
    EditorDataMountPlan plan;
    const std::filesystem::path modsDirectory = gameDataDirectory / "mods";

    for (const std::filesystem::path& dataPath : editorDataPaths) {
        if (dataPath.empty() || isSameOrInside(dataPath, gameDataDirectory)) {
            // Already reachable: master_patches, the game's own DATs or the existing mod list.
            continue;
        }

        // Lexical on purpose: std::filesystem::relative() resolves against the current working
        // directory and the current volume, which makes the result depend on where the editor happens
        // to run. lexically_relative() yields an empty path when no relative route exists, e.g. across
        // Windows volumes. A semicolon or hash would make the engine's parser treat the entry as a
        // comment and silently skip it.
        const std::string text
            = dataPath.lexically_normal().lexically_relative(modsDirectory.lexically_normal()).generic_string();
        if (text.empty() || text.find_first_of(";#") != std::string::npos) {
            plan.unmountable.push_back(dataPath);
            continue;
        }

        plan.modsOrderEntries.push_back(text);
    }

    return plan;
}

std::string applyManagedModsOrderBlock(const std::string& existingContent, const std::vector<std::string>& entries) {
    std::string content;
    std::string lineEnding = "\n";
    bool lineEndingKnown = false;
    bool insideManagedBlock = false;

    std::istringstream stream(existingContent);
    std::string line;
    while (std::getline(stream, line)) {
        const bool crlf = line.ends_with("\r");
        if (!lineEndingKnown) {
            lineEnding = crlf ? "\r\n" : "\n";
            lineEndingKnown = true;
        }

        const std::string_view text = trimmed(line);
        if (insideManagedBlock) {
            insideManagedBlock = text != kManagedBlockEnd;
            continue;
        }
        if (text == kManagedBlockBegin) {
            insideManagedBlock = true;
            continue;
        }

        // `line` still carries its CR when the file is CRLF, so appending LF restores the pair.
        content += line + "\n";
    }

    if (entries.empty()) {
        return content;
    }

    // Appended last: the engine prepends every mount it opens, so the final entries win.
    content += std::string(kManagedBlockBegin) + lineEnding;
    for (const std::string& entry : entries) {
        content += entry + lineEnding;
    }
    content += std::string(kManagedBlockEnd) + lineEnding;
    return content;
}

std::vector<std::string> collectLaunchConfigurationWarnings(
    const std::filesystem::path& gameDataDirectory,
    const std::filesystem::path& executableDirectory,
    bool executableDirectoryHasGameData,
    const std::vector<std::filesystem::path>& unmountableDataPaths) {
    std::vector<std::string> warnings;

    if (executableDirectoryHasGameData && !isSameOrInside(executableDirectory, gameDataDirectory)) {
        warnings.push_back("The game executable sits in " + executableDirectory.string()
            + ", which holds its own Fallout 2 data. Gecko runs the game against "
            + gameDataDirectory.string() + " instead, so that is the installation the map is written to.");
    }

    if (!unmountableDataPaths.empty()) {
        std::string paths;
        for (const std::filesystem::path& dataPath : unmountableDataPaths) {
            paths += (paths.empty() ? "" : ", ") + dataPath.string();
        }
        warnings.push_back("These editor data paths cannot be mounted into " + gameDataDirectory.string() + ": "
            + paths + ". Files that exist only there will be missing from the game.");
    }

    return warnings;
}

bool GameLauncher::confirmLaunchConfiguration(const std::filesystem::path& gameDataDirectory,
    const std::filesystem::path& executablePath,
    const std::vector<std::filesystem::path>& unmountableDataPaths) const {
    const std::filesystem::path executableDirectory = executablePath.parent_path();
    const std::vector<std::string> warnings = collectLaunchConfigurationWarnings(gameDataDirectory,
        executableDirectory, util::hasFallout2DataLayout(executableDirectory), unmountableDataPaths);
    if (warnings.empty()) {
        return true;
    }

    QString message;
    for (const std::string& warning : warnings) {
        spdlog::warn("Launch configuration: {}", warning);
        message += QString::fromStdString(warning) + "\n\n";
    }
    message += "Play anyway?";

    return QtDialogs::showQuestion(_dialogParent, "Launch Configuration", message);
}

bool GameLauncher::modifyDdrawIni(const std::filesystem::path& ddrawIniPath, const std::string& mapFilename) const {
    const bool patched = patchConfigFile(ddrawIniPath, [&mapFilename](const std::string& content) {
        return applyStartingMapToDdrawIni(content, mapFilename);
    });
    if (patched) {
        spdlog::debug("Modified {}: set StartingMap to {}", ddrawIniPath.string(), mapFilename);
    }
    return patched;
}

bool GameLauncher::writeContentConfigPatch(const std::filesystem::path& gameDataDirectory,
    const std::string& mapFilename) const {
    const std::filesystem::path configPath = gameDataDirectory / "data" / "config" / "game#patch.cfg";
    const bool patched = patchConfigFile(configPath, [&mapFilename](const std::string& content) {
        return applyStartingMapToContentConfig(content, mapFilename);
    });
    if (patched) {
        spdlog::debug("Modified {}: set [start] map to {}", configPath.string(), mapFilename);
    }
    return patched;
}

bool GameLauncher::writeModsOrder(const std::filesystem::path& gameDataDirectory,
    const std::vector<std::string>& entries) {
    const std::filesystem::path modsOrderPath = gameDataDirectory / "mods" / "mods_order.txt";

    // Capture the player's own load order once per launch. A second Play before the game exits must
    // not record our own block as the file to restore, and a block left behind by an editor crash is
    // stripped so it never becomes part of the "original".
    if (!_modsOrderPatched) {
        const std::optional<std::string> existing = readFileVerbatim(modsOrderPath);
        _modsOrderPath = modsOrderPath;
        // Kept byte for byte so the restore reproduces the file exactly, including a missing final
        // newline - unless it still carries a block from an editor that died before restoring, which
        // is not part of the player's own order and is dropped.
        _modsOrderOriginal = existing.has_value() && existing->find(kManagedBlockBegin) != std::string::npos
            ? std::optional<std::string>(applyManagedModsOrderBlock(*existing, {}))
            : existing;
    }

    if (!writeFileVerbatim(modsOrderPath,
            applyManagedModsOrderBlock(_modsOrderOriginal.value_or(std::string{}), entries))) {
        return false;
    }

    _modsOrderPatched = true;
    spdlog::debug("Mounted {} editor data path(s) through {}", entries.size(), modsOrderPath.string());
    return true;
}

void GameLauncher::restoreModsOrder() {
    if (!_modsOrderPatched) {
        return;
    }
    _modsOrderPatched = false;

    if (_modsOrderOriginal.has_value()) {
        if (writeFileVerbatim(_modsOrderPath, *_modsOrderOriginal)) {
            spdlog::debug("Restored {}", _modsOrderPath.string());
        }
        return;
    }

    // The game had no mod list before; leave the directory as we found it.
    std::error_code ec;
    std::filesystem::remove(_modsOrderPath, ec);
    if (ec) {
        spdlog::warn("Failed to remove {}: {}", _modsOrderPath.string(), ec.message());
    }
}

void GameLauncher::launchGame(const std::filesystem::path& executablePath,
    const std::filesystem::path& workingDirectory) {
    const std::filesystem::path binary = resolveLaunchBinary(executablePath);
    spdlog::debug("Launching game executable: {} (working directory: {})",
        binary.empty() ? executablePath.string() : binary.string(), workingDirectory.string());

    QProcess* gameProcess = new QProcess(this);
    gameProcess->setWorkingDirectory(QString::fromStdString(workingDirectory.string()));

    // 'open' returns as soon as LaunchServices accepts the request, long before the game has read
    // its mod list, so on that path the restore has to wait for the editor to shut down instead.
    const bool restoreOnExit = !binary.empty();

    connect(gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, gameProcess, restoreOnExit](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::CrashExit) {
                spdlog::warn("Game process crashed with exit code: {}", exitCode);
            } else {
                spdlog::debug("Game process finished with exit code: {}", exitCode);
            }
            if (restoreOnExit) {
                restoreModsOrder();
            }
            gameProcess->deleteLater();
        });

    connect(gameProcess, &QProcess::errorOccurred,
        [this, gameProcess](QProcess::ProcessError error) {
            QString errorMsg;
            switch (error) {
                case QProcess::FailedToStart:
                    errorMsg = "Failed to start the game process";
                    break;
                case QProcess::Crashed:
                    errorMsg = "Game process crashed";
                    break;
                default:
                    errorMsg = "Unknown error occurred while running the game";
                    break;
            }
            QtDialogs::showError(_dialogParent, "Game Launch Error", errorMsg);
            spdlog::error("Game process error: {}", errorMsg.toStdString());
            restoreModsOrder();
            gameProcess->deleteLater();
        });

    if (binary.empty()) {
        // Nothing executable inside the bundle: fall back to 'open', which starts the app through
        // LaunchServices and therefore ignores the working directory set above.
        spdlog::warn("No binary found inside {}; falling back to 'open'. The game will resolve its "
                     "data paths against its own working directory, not {}.",
            executablePath.string(), workingDirectory.string());
        gameProcess->start("open", QStringList() << QString::fromStdString(executablePath.string()));
    } else {
        gameProcess->start(QString::fromStdString(binary.string()));
    }

    if (!gameProcess->waitForStarted(5000)) {
        QtDialogs::showError(_dialogParent, "Game Launch Failed",
            "Failed to start the game within 5 seconds.");
        restoreModsOrder();
        gameProcess->deleteLater();
        return;
    }

    _showStatus("Game launched successfully!");
}

} // namespace geck
