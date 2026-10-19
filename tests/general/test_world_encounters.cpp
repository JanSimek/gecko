#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <sstream>
#include <string>

#include "cli/WorldEncounters.h"
#include "resource/GameResources.h"

#include "support/CliJson.h"
#include "support/DataTree.h"

using namespace geck;
namespace fs = std::filesystem;

TEST_CASE("world encounters report the tables and the odds at a worldmap position", "[world_encounters][cli]") {
    test::DataTree tree{ "world_encounters_data" };
    tree.write("data/vault13.gam", "GAME_GLOBAL_VARS:\nGVAR_REP :=0;\nGVAR_HUNTERS :=0;\n");
    tree.write("data/worldmap.txt", "[Data]\nNone=0%\nRare=4%\nUncommon=12%\nCommon=22%\nFrequent=38%\nForced=100%\n"
                                    "terrain_types=Desert:1\n"
                                    "[Encounter Table 0]\nlookup_name=DstN\nmaps=Desert Encounter 1\n"
                                    "enc_00=Chance:50%,Enc:(2-5) Raiders AMBUSH Player,If(Global(1) > 1)\n"
                                    "[Encounter Table 1]\nlookup_name=DstS\n"
                                    "enc_00=Chance:50%,Enc:(2-5) Raiders AMBUSH Player\n"
                                    "enc_01=Chance:50%,Enc:(3-8) Rats AMBUSH Player\n"
                                    "[Tile Data]\nnum_horizontal_tiles=1\n"
                                    "[Tile 0]\nart_idx=339\nencounter_difficulty=2\n"
                                    "1_0=Desert, Fill_N, Common, Frequent, Rare, dstn\n");
    tree.write("text/english/game/worldmap.msg", "{3000}{}{A party of raiders.}\n{3051}{}{Some rats.}\n");

    auto& resources = tree.mount();

    SECTION("the whole table set, with the gvars a condition reads") {
        const auto j = test::runJson([&](std::ostream& out) { return cli::buildWorldEncounters(resources, out); });
        CHECK(j["frequencies"]["frequent"] == 38);
        REQUIRE(j["encounterTables"].size() == 2);
        const auto& entry = j["encounterTables"][0]["entries"][0];
        CHECK(entry["groups"][0]["name"] == "Raiders");
        CHECK(entry["conditionGvars"][0]["index"] == 1);
        CHECK(entry["conditionGvars"][0]["name"] == "GVAR_HUNTERS");
        // worldmap.msg 3000 + 50 * table + entry.
        CHECK(entry["description"] == "A party of raiders.");
        CHECK(j["encounterTables"][1]["entries"][0]["description"].is_null()); // 3050: no message
        CHECK(j["encounterTables"][1]["entries"][1]["description"] == "Some rats.");
    }

    SECTION("one position: its sub-tile, the odds per part of the day and the table") {
        // row 1, column 0
        const auto j = test::runJson([&](std::ostream& out) { return cli::describeWorldPosition(resources, 60, 10, out); });
        CHECK(j["subtile"]["row"] == 1);
        CHECK(j["terrain"] == "Desert");
        CHECK(j["fill"] == "fill_n");
        CHECK(j["encounterDifficulty"] == 2);
        CHECK(j["chances"]["morning"]["frequency"] == "common");
        CHECK(j["chances"]["morning"]["percent"] == 22);
        CHECK(j["chances"]["afternoon"]["percent"] == 38);
        CHECK(j["chances"]["night"]["frequency"] == "rare");
        CHECK(j["encounterTable"]["lookupName"] == "DstN");
    }

    SECTION("a position off the worldmap is an error") {
        std::ostringstream oss;
        CHECK(cli::describeWorldPosition(resources, 400, 10, oss) != 0);
    }
}
