#include <catch2/catch_test_macros.hpp>

#include "state/GameLauncher.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace geck;

TEST_CASE("applyStartingMapToContentConfig sets the fallout2-ce start map", "[game_launcher]") {
    SECTION("The section and key are created for an empty config") {
        const std::string result = applyStartingMapToContentConfig("", "artemple.map");

        REQUIRE(result.find("[start]") != std::string::npos);
        REQUIRE(result.find("map=artemple.map") != std::string::npos);
    }

    SECTION("An existing map entry is replaced and other sections are preserved") {
        const std::string input
            = "[system]\n"
              "language=english\n"
              "[start]\n"
              "map=oldmap.map\n"
              "worldmap_x=100\n";

        const std::string result = applyStartingMapToContentConfig(input, "newmap.map");

        REQUIRE(result.find("map=newmap.map") != std::string::npos);
        REQUIRE(result.find("oldmap.map") == std::string::npos);
        REQUIRE(result.find("worldmap_x=100") != std::string::npos);
        REQUIRE(result.find("language=english") != std::string::npos);
    }

    SECTION("The key is inserted into an existing section before the next one starts") {
        const std::string input
            = "[start]\n"
              "worldmap_x=100\n"
              "[sound]\n"
              "volume=5\n";

        const std::string result = applyStartingMapToContentConfig(input, "fresh.map");

        REQUIRE(result.find("map=fresh.map") < result.find("[sound]"));
        REQUIRE(result.find("volume=5") != std::string::npos);
    }

    SECTION("A key of the same name in another section is left alone") {
        const std::string input
            = "[debug]\n"
              "map=leave.me\n"
              "[start]\n"
              "worldmap_x=1\n";

        const std::string result = applyStartingMapToContentConfig(input, "target.map");

        REQUIRE(result.find("map=leave.me") != std::string::npos);
        REQUIRE(result.find("map=target.map") != std::string::npos);
    }
}

TEST_CASE("Starting-map rewrites survive CRLF config files", "[game_launcher]") {
    // A real ddraw.ini ships CRLF. Section headers must still match, and the terminators of
    // untouched lines must be preserved.
    SECTION("A CRLF [Misc] section is recognised rather than duplicated") {
        const std::string input
            = "[Misc]\r\n"
              "StartingMap=old.map\r\n"
              "ScrollDist=10\r\n";

        const std::string result = applyStartingMapToDdrawIni(input, "new.map");

        REQUIRE(result.find("StartingMap=new.map") != std::string::npos);
        REQUIRE(result.find("old.map") == std::string::npos);
        REQUIRE(result.find("ScrollDist=10\r\n") != std::string::npos);
        // Exactly one [Misc] section: the header must not have been appended a second time.
        REQUIRE(result.find("[Misc]", result.find("[Misc]") + 1) == std::string::npos);
    }

    SECTION("A key added to a CRLF file uses CRLF too") {
        const std::string input
            = "[Misc]\r\n"
              "ScrollDist=10\r\n";

        const std::string result = applyStartingMapToDdrawIni(input, "new.map");

        REQUIRE(result.find("StartingMap=new.map\r\n") != std::string::npos);
    }
}

TEST_CASE("collectLaunchConfigurationWarnings reports incoherent launch setups", "[game_launcher]") {
    const std::filesystem::path gameDir = "/games/fallout2";

    SECTION("A data path inside the game directory is coherent") {
        const std::vector<std::filesystem::path> dataPaths = { "/games/fallout2/data", "/games/fallout2/master.dat" };

        REQUIRE(collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", false, dataPaths).empty());
    }

    SECTION("A data path containing the game directory is coherent") {
        const std::vector<std::filesystem::path> dataPaths = { "/games" };

        REQUIRE(collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", false, dataPaths).empty());
    }

    SECTION("Editing a different tree than the one being played is reported") {
        const std::vector<std::filesystem::path> dataPaths = { "/mods/restoration-project/data" };

        const auto warnings = collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", false, dataPaths);

        REQUIRE(warnings.size() == 1);
        REQUIRE(warnings[0].find("/games/fallout2") != std::string::npos);
    }

    SECTION("An executable in a separate installation is reported") {
        const std::vector<std::filesystem::path> dataPaths = { "/games/fallout2/data" };

        const auto warnings = collectLaunchConfigurationWarnings(gameDir, "/other/fallout2", true, dataPaths);

        REQUIRE(warnings.size() == 1);
        REQUIRE(warnings[0].find("/other/fallout2") != std::string::npos);
    }

    SECTION("An executable inside the game directory is not reported") {
        const std::vector<std::filesystem::path> dataPaths = { "/games/fallout2/data" };

        REQUIRE(collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", true, dataPaths).empty());
    }

    SECTION("A trailing separator does not make paths look different") {
        const std::vector<std::filesystem::path> dataPaths = { "/games/fallout2/data/" };

        REQUIRE(collectLaunchConfigurationWarnings("/games/fallout2/", "/games/fallout2", false, dataPaths).empty());
    }

    SECTION("A sibling directory sharing a name prefix is not treated as inside") {
        const std::vector<std::filesystem::path> dataPaths = { "/games/fallout2-other/data" };

        REQUIRE(collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", false, dataPaths).size() == 1);
    }

    SECTION("No configured data paths yields no data-path warning") {
        REQUIRE(collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", false, {}).empty());
    }
}
