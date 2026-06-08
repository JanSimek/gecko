#include "GameLauncher.h"

#include "resource/GameResources.h"
#include "util/Settings.h"
#include "util/QtDialogs.h"
#include "util/ProHelper.h"
#include "format/pro/Pro.h"
#include "writer/map/MapWriter.h"

#include <fstream>
#include <sstream>

#include <QProcess>
#include <QStringList>
#include <spdlog/spdlog.h>

namespace geck {

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

    // Steam installs launch via App ID and don't need the map file copied
    if (settings.getGameInstallationType() == Settings::GameInstallationType::STEAM) {
        launchGameViaSteam(settings.getSteamAppId());
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

    std::filesystem::path mapsDir = gameDataDir / "data" / "maps";
    std::filesystem::path mapDestination = mapsDir / mapFilename;

    _showStatus(QString("Playing map: %1").arg(QString::fromStdString(mapFilename)));

    try {
        // 1. Save the current map to the game directory
        if (!std::filesystem::exists(mapsDir)) {
            std::filesystem::create_directories(mapsDir);
        }

        MapWriter mapWriter{ [this](int32_t PID) {
            return _resources.repository().load<Pro>(ProHelper::basePath(_resources, PID));
        } };
        mapWriter.openFile(mapDestination.string());

        if (!mapWriter.write(*mapFile)) {
            QtDialogs::showError(_dialogParent, "Save Failed",
                QString("Failed to save map to game directory: %1").arg(QString::fromStdString(mapDestination.string())));
            return;
        }

        spdlog::info("Saved map to game directory: {} ({} bytes)", mapDestination.string(), mapWriter.getBytesWritten());

        // 2. Modify ddraw.ini
        std::filesystem::path ddrawIniPath = gameDataDir / "ddraw.ini";
        if (!modifyDdrawIni(ddrawIniPath, mapFilename)) {
            QtDialogs::showWarning(_dialogParent, "Configuration Warning",
                "Map saved successfully, but failed to modify ddraw.ini. You may need to manually set the starting map.");
        }

        // 3. Launch the game
        launchGame(executableLocation);

    } catch (const std::exception& e) {
        QtDialogs::showError(_dialogParent, "Play Failed",
            QString("Failed to play map: %1").arg(e.what()));
        spdlog::error("Failed to play map: {}", e.what());
    }
}

std::string applyStartingMapToDdrawIni(const std::string& iniContent, const std::string& mapFilename) {
    std::string content;
    bool foundMiscSection = false;
    bool foundStartingMap = false;

    std::istringstream stream(iniContent);
    std::string line;
    std::string currentSection;

    while (std::getline(stream, line)) {
        if (line.starts_with("[") && line.ends_with("]")) {
            currentSection = line;
            if (line == "[Misc]") {
                foundMiscSection = true;
            }
        }

        // Replace StartingMap inside [Misc] (also matches a commented-out ;StartingMap=)
        if (foundMiscSection && currentSection == "[Misc]" && (line.starts_with("StartingMap=") || line.starts_with(";StartingMap="))) {
            foundStartingMap = true;
            line = "StartingMap=" + mapFilename;
        }

        content += line + "\n";
    }

    // If no [Misc] section found, add it
    if (!foundMiscSection) {
        content += "\n[Misc]\n";
        foundMiscSection = true;
    }

    // If no StartingMap found in [Misc] section, add it after the section header
    if (!foundStartingMap && foundMiscSection) {
        size_t miscPos = content.find("[Misc]");
        if (miscPos != std::string::npos) {
            size_t nextSection = content.find("\n[", miscPos + 6);
            if (nextSection != std::string::npos) {
                content.insert(nextSection, "StartingMap=" + mapFilename + "\n");
            } else {
                content += "StartingMap=" + mapFilename + "\n";
            }
        }
    }

    return content;
}

bool GameLauncher::modifyDdrawIni(const std::filesystem::path& ddrawIniPath, const std::string& mapFilename) {
    try {
        std::string fileContent;

        // Read existing file if it exists
        if (std::filesystem::exists(ddrawIniPath)) {
            std::ifstream file(ddrawIniPath);
            if (!file.is_open()) {
                spdlog::error("Failed to open ddraw.ini for reading: {}", ddrawIniPath.string());
                return false;
            }

            std::string line;
            while (std::getline(file, line)) {
                fileContent += line + "\n";
            }
            file.close();
        }

        std::string content = applyStartingMapToDdrawIni(fileContent, mapFilename);

        // Write the modified content back
        std::ofstream outFile(ddrawIniPath);
        if (!outFile.is_open()) {
            spdlog::error("Failed to open ddraw.ini for writing: {}", ddrawIniPath.string());
            return false;
        }

        outFile << content;
        outFile.close();

        spdlog::info("Modified {}: set StartingMap to {}", ddrawIniPath.string(), mapFilename);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("Failed to modify {}: {}", ddrawIniPath.string(), e.what());
        return false;
    }
}

void GameLauncher::launchGame(const std::filesystem::path& executablePath) {
    spdlog::info("Launching game executable: {}", executablePath.string());

    QProcess* gameProcess = new QProcess(this);
    gameProcess->setWorkingDirectory(QString::fromStdString(executablePath.parent_path().string()));

    connect(gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [gameProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::CrashExit) {
                spdlog::warn("Game process crashed with exit code: {}", exitCode);
            } else {
                spdlog::info("Game process finished with exit code: {}", exitCode);
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

    // Launch the executable (use 'open' on macOS for .app bundles)
    if (executablePath.extension() == ".app") {
        gameProcess->start("open", QStringList() << QString::fromStdString(executablePath.string()));
    } else {
        gameProcess->start(QString::fromStdString(executablePath.string()));
    }

    if (!gameProcess->waitForStarted(5000)) {
        QtDialogs::showError(_dialogParent, "Game Launch Failed",
            "Failed to start the game within 5 seconds.");
        gameProcess->deleteLater();
        return;
    }

    _showStatus("Game launched successfully!");
    spdlog::info("Game launched successfully");
}

void GameLauncher::launchGameViaSteam(const std::string& appId) {
    spdlog::info("Launching Fallout 2 via Steam with App ID: {}", appId);

    QProcess* steamProcess = new QProcess(this);

    connect(steamProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [steamProcess](int exitCode, [[maybe_unused]] QProcess::ExitStatus exitStatus) {
            spdlog::info("Steam launch finished with exit code: {}", exitCode);
            steamProcess->deleteLater();
        });

    connect(steamProcess, &QProcess::errorOccurred,
        [this, steamProcess, appId]([[maybe_unused]] QProcess::ProcessError error) {
            QString errorMsg = QString("Failed to launch Fallout 2 via Steam (App ID: %1).\n\n"
                                       "Please ensure Steam is installed and Fallout 2 is available in your Steam library.")
                                   .arg(QString::fromStdString(appId));
            QtDialogs::showError(_dialogParent, "Steam Launch Error", errorMsg);
            spdlog::error("Failed to launch game via Steam: App ID {}", appId);
            steamProcess->deleteLater();
        });

    QString steamUrl = QString("steam://run/%1").arg(QString::fromStdString(appId));

#ifdef __APPLE__
    steamProcess->start("open", QStringList() << steamUrl);
#elif defined(_WIN32)
    steamProcess->start("cmd", QStringList() << "/c" << "start" << steamUrl);
#else
    // Linux - try xdg-open first, then steam directly
    steamProcess->start("xdg-open", QStringList() << steamUrl);
#endif

    _showStatus(QString("Launching Fallout 2 via Steam (App ID: %1)...").arg(QString::fromStdString(appId)));
}

} // namespace geck
