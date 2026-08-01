#include "GameLauncher.h"

#include "resource/GameResources.h"
#include "state/MapSaveService.h"
#include "ui/Settings.h"
#include "ui/QtDialogs.h"
#include "util/GameDataPathResolver.h"

#include <algorithm>
#include <fstream>
#include <sstream>
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
        try {
            std::string fileContent;
            if (std::filesystem::exists(path)) {
                std::ifstream file(path, std::ios::binary);
                if (!file.is_open()) {
                    spdlog::error("Failed to open {} for reading", path.string());
                    return false;
                }
                std::ostringstream buffer;
                buffer << file.rdbuf();
                fileContent = buffer.str();
            } else if (!path.parent_path().empty()) {
                std::filesystem::create_directories(path.parent_path());
            }

            std::ofstream outFile(path, std::ios::binary);
            if (!outFile.is_open()) {
                spdlog::error("Failed to open {} for writing", path.string());
                return false;
            }

            outFile << transform(fileContent);
            return outFile.good();
        } catch (const std::exception& e) {
            spdlog::error("Failed to update {}: {}", path.string(), e.what());
            return false;
        }
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

    if (!confirmLaunchConfiguration(gameDataDir, executableLocation)) {
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

std::vector<std::string> collectLaunchConfigurationWarnings(
    const std::filesystem::path& gameDataDirectory,
    const std::filesystem::path& executableDirectory,
    bool executableDirectoryHasGameData,
    const std::vector<std::filesystem::path>& editorDataPaths) {
    std::vector<std::string> warnings;

    if (executableDirectoryHasGameData && !isSameOrInside(executableDirectory, gameDataDirectory)) {
        warnings.push_back("The game executable sits in " + executableDirectory.string()
            + ", which holds its own Fallout 2 data. Gecko runs the game against "
            + gameDataDirectory.string() + " instead, so that is the installation the map is written to.");
    }

    const auto coversGameDirectory = [&gameDataDirectory](const std::filesystem::path& dataPath) {
        return isSameOrInside(dataPath, gameDataDirectory) || isSameOrInside(gameDataDirectory, dataPath);
    };
    if (!editorDataPaths.empty() && !std::ranges::any_of(editorDataPaths, coversGameDirectory)) {
        warnings.push_back("None of the editor's data paths point into " + gameDataDirectory.string()
            + ". The game loads only what that installation already contains, so protos, art or scripts "
              "that exist just in the editor's data paths will be missing.");
    }

    return warnings;
}

bool GameLauncher::confirmLaunchConfiguration(const std::filesystem::path& gameDataDirectory,
    const std::filesystem::path& executablePath) const {
    const std::filesystem::path executableDirectory = executablePath.parent_path();
    const std::vector<std::string> warnings = collectLaunchConfigurationWarnings(gameDataDirectory,
        executableDirectory, util::hasFallout2DataLayout(executableDirectory), _settings->getDataPaths());
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

void GameLauncher::launchGame(const std::filesystem::path& executablePath,
    const std::filesystem::path& workingDirectory) {
    const std::filesystem::path binary = resolveLaunchBinary(executablePath);
    spdlog::debug("Launching game executable: {} (working directory: {})",
        binary.empty() ? executablePath.string() : binary.string(), workingDirectory.string());

    QProcess* gameProcess = new QProcess(this);
    gameProcess->setWorkingDirectory(QString::fromStdString(workingDirectory.string()));

    connect(gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [gameProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::CrashExit) {
                spdlog::warn("Game process crashed with exit code: {}", exitCode);
            } else {
                spdlog::debug("Game process finished with exit code: {}", exitCode);
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
        gameProcess->deleteLater();
        return;
    }

    _showStatus("Game launched successfully!");
}

} // namespace geck
