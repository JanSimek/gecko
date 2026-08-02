#include <catch2/catch_test_macros.hpp>

#include "state/GameLauncher.h"

#include <filesystem>
#include <sstream>
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

TEST_CASE("planEditorDataMounts maps editor data paths onto the mod load order", "[game_launcher]") {
    const std::filesystem::path gameDir = "/games/fallout2";

    SECTION("Paths already inside the game directory need no mount") {
        const std::vector<std::filesystem::path> dataPaths = {
            "/games/fallout2", "/games/fallout2/data", "/games/fallout2/master.dat", "/games/fallout2/mods/rpu.dat"
        };

        const auto plan = planEditorDataMounts(gameDir, dataPaths);

        REQUIRE(plan.modsOrderEntries.empty());
        REQUIRE(plan.unmountable.empty());
    }

    SECTION("An outside path becomes an entry relative to the mods directory") {
        const auto plan = planEditorDataMounts(gameDir, { "/mods/restoration-project/data" });

        REQUIRE(plan.modsOrderEntries == std::vector<std::string>{ "../../../mods/restoration-project/data" });
        REQUIRE(plan.unmountable.empty());
    }

    SECTION("Editor order is preserved so the last data path keeps winning") {
        // Exact entries on purpose: the mapping has to be purely lexical, identical on every
        // platform and independent of the working directory or whether these paths exist.
        const auto plan = planEditorDataMounts(gameDir, { "/a/first.dat", "/games/fallout2/data", "/b/second" });

        REQUIRE(plan.modsOrderEntries
            == std::vector<std::string>{ "../../../a/first.dat", "../../../b/second" });
        REQUIRE(plan.unmountable.empty());
    }

    SECTION("A path the engine's parser would treat as a comment is reported instead") {
        // The mod list parser skips any line containing ';' or '#'.
        const auto plan = planEditorDataMounts(gameDir, { "/mods/patch#3/data" });

        REQUIRE(plan.modsOrderEntries.empty());
        REQUIRE(plan.unmountable == std::vector<std::filesystem::path>{ "/mods/patch#3/data" });
    }

    SECTION("A sibling directory sharing a name prefix is not treated as inside") {
        const auto plan = planEditorDataMounts(gameDir, { "/games/fallout2-other/data" });

        REQUIRE(plan.modsOrderEntries.size() == 1);
    }
}

TEST_CASE("applyManagedModsOrderBlock keeps the player's own load order", "[game_launcher]") {
    const std::string playerOrder
        = "rpu.dat\n"
          "party_orders.dat\n"
          "npc_armor.dat\n";

    SECTION("Entries are appended last so editor data outranks the player's mods") {
        const std::string result = applyManagedModsOrderBlock(playerOrder, { "../../rp/data" });

        REQUIRE(result.starts_with(playerOrder));
        REQUIRE(result.find("../../rp/data") > result.find("npc_armor.dat"));
    }

    SECTION("A previous block is replaced rather than stacked") {
        const std::string once = applyManagedModsOrderBlock(playerOrder, { "../../rp/data" });
        const std::string twice = applyManagedModsOrderBlock(once, { "../../other/data" });

        REQUIRE(twice.find("../../rp/data") == std::string::npos);
        REQUIRE(twice.find("../../other/data") != std::string::npos);
        REQUIRE(twice.starts_with(playerOrder));
    }

    SECTION("Passing no entries restores the file to the player's own order") {
        const std::string patched = applyManagedModsOrderBlock(playerOrder, { "../../rp/data", "../../x.dat" });

        REQUIRE(applyManagedModsOrderBlock(patched, {}) == playerOrder);
    }

    SECTION("The block markers are comments, so the engine skips them") {
        const std::string result = applyManagedModsOrderBlock(playerOrder, { "../../rp/data" });

        REQUIRE(result.find("; gecko: editor data paths") != std::string::npos);
        REQUIRE(result.find("; gecko: end of editor data paths") != std::string::npos);
        // Every line the engine would try to mount is either a player entry or one of ours.
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line)) {
            const bool isComment = line.find_first_of(";#") != std::string::npos;
            const bool isKnown = line == "rpu.dat" || line == "party_orders.dat" || line == "npc_armor.dat"
                || line == "../../rp/data" || line.empty();
            REQUIRE((isComment || isKnown));
        }
    }

    SECTION("A CRLF mod list stays CRLF") {
        const std::string result = applyManagedModsOrderBlock("rpu.dat\r\n", { "../../rp/data" });

        REQUIRE(result.find("rpu.dat\r\n") != std::string::npos);
        REQUIRE(result.find("../../rp/data\r\n") != std::string::npos);
    }
}

TEST_CASE("collectLaunchConfigurationWarnings reports incoherent launch setups", "[game_launcher]") {
    const std::filesystem::path gameDir = "/games/fallout2";

    SECTION("A coherent setup produces no warnings") {
        REQUIRE(collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", false, {}).empty());
    }

    SECTION("An executable in a separate installation is reported") {
        const auto warnings = collectLaunchConfigurationWarnings(gameDir, "/other/fallout2", true, {});

        REQUIRE(warnings.size() == 1);
        REQUIRE(warnings[0].find("/other/fallout2") != std::string::npos);
    }

    SECTION("An executable inside the game directory is not reported") {
        REQUIRE(collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", true, {}).empty());
    }

    SECTION("A trailing separator does not make paths look different") {
        REQUIRE(collectLaunchConfigurationWarnings("/games/fallout2/", "/games/fallout2", true, {}).empty());
    }

    SECTION("Data paths that cannot be mounted are named") {
        const auto warnings = collectLaunchConfigurationWarnings(gameDir, "/games/fallout2", false,
            { "/elsewhere/data", "/other#path/data" });

        REQUIRE(warnings.size() == 1);
        REQUIRE(warnings[0].find("/elsewhere/data") != std::string::npos);
        REQUIRE(warnings[0].find("/other#path/data") != std::string::npos);
    }
}
