#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "cli/WorldEncounters.h"
#include "resource/GameResources.h"

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

} // namespace

TEST_CASE("world encounters report the tables and the odds at a worldmap position", "[world_encounters]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "world_encounters_data";
    fs::remove_all(root);
    writeFile(root / "data" / "vault13.gam", "GAME_GLOBAL_VARS:\nGVAR_REP :=0;\nGVAR_HUNTERS :=0;\n");
    writeFile(root / "data" / "worldmap.txt", "[Data]\nNone=0%\nRare=4%\nUncommon=12%\nCommon=22%\nFrequent=38%\nForced=100%\n"
                                              "terrain_types=Desert:1\n"
                                              "[Encounter Table 0]\nlookup_name=DstN\nmaps=Desert Encounter 1\n"
                                              "enc_00=Chance:50%,Enc:(2-5) Raiders AMBUSH Player,If(Global(1) > 1)\n"
                                              "[Tile Data]\nnum_horizontal_tiles=1\n"
                                              "[Tile 0]\nart_idx=339\nencounter_difficulty=2\n"
                                              "1_0=Desert, Fill_N, Common, Frequent, Rare, dstn\n");

    resource::GameResources resources;
    resources.files().addDataPath(root.string());

    SECTION("the whole table set, with the gvars a condition reads") {
        std::ostringstream oss;
        REQUIRE(cli::buildWorldEncounters(resources, oss) == 0);
        const auto j = nlohmann::json::parse(oss.str());
        CHECK(j["frequencies"]["frequent"] == 38);
        REQUIRE(j["encounterTables"].size() == 1);
        const auto& entry = j["encounterTables"][0]["entries"][0];
        CHECK(entry["groups"][0]["name"] == "Raiders");
        CHECK(entry["conditionGvars"][0]["index"] == 1);
        CHECK(entry["conditionGvars"][0]["name"] == "GVAR_HUNTERS");
    }

    SECTION("one position: its sub-tile, the odds per part of the day and the table") {
        std::ostringstream oss;
        REQUIRE(cli::describeWorldPosition(resources, 60, 10, oss) == 0); // row 1, column 0
        const auto j = nlohmann::json::parse(oss.str());
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
