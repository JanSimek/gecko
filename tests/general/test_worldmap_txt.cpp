#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "format/worldmap/WorldmapTxt.h"
#include "reader/worldmap/WorldmapTxtReader.h"

using namespace geck;

namespace {
// A trimmed-down worldmap.txt: the [Data] terrain lists, an ignored [Random Maps] section, and two
// [Encounter: NAME] groups whose type_NN lines carry the real syntax (Ratio:NN%, pid, Item:N{suffix},
// the bare "Dead" flag, Script:NN) plus inline ';' comments the reader must strip.
constexpr const char* kWorldmapTxt = R"(; worldmap data
[Data]
Frequent=43%       ; ignored frequency
terrain_types=Desert:1, Mountain:2, City:1, Ocean:1
terrain_short_names=DES, MNT, CTY, OCN ; WIP

[Random Maps: Desert]   ; not parsed
map_00=desert1

[Encounter: Raiders]  ; WIP!
type_00=Ratio:35%, pid:16777252, Item:8{Wielded}, Item:40, Script:255
type_01=Dead, pid:16777220, Item:40, Script:256

[Encounter: ARRO_Rats]
type_00=Ratio:100%, pid:16777276
)";
} // namespace

TEST_CASE("parseWorldmapTxt reads terrain types and encounter groups", "[worldmap]") {
    const WorldmapTxt world = parseWorldmapTxt(std::string{ kWorldmapTxt });

    REQUIRE(world.terrains.size() == 4);
    CHECK(world.terrains[0].name == "Desert");
    CHECK(world.terrains[0].shortName == "DES");
    CHECK(world.terrains[0].weight == 1);
    CHECK(world.terrains[1].name == "Mountain");
    CHECK(world.terrains[1].weight == 2);
    CHECK(world.terrains[3].shortName == "OCN");

    REQUIRE(world.encounters.size() == 2);
    const Encounter* raiders = world.findEncounter("Raiders");
    REQUIRE(raiders != nullptr);
    REQUIRE(raiders->entries.size() == 2);
    CHECK(raiders->entries[0].pid == 16777252);
    CHECK(raiders->entries[0].ratioPercent == 35); // "35%" parsed
    CHECK(raiders->entries[0].dead == false);
    CHECK(raiders->entries[0].script == 255);
    CHECK(raiders->entries[0].items == std::vector<int>{ 8, 40 }); // "8{Wielded}" -> 8
    CHECK(raiders->entries[1].dead == true);                       // bare "Dead" flag
    CHECK(raiders->entries[1].pid == 16777220);

    const Encounter* rats = world.findEncounter("ARRO_Rats");
    REQUIRE(rats != nullptr);
    REQUIRE(rats->entries.size() == 1);
    CHECK(rats->entries[0].pid == 16777276);
    CHECK(rats->entries[0].ratioPercent == 100);

    CHECK(world.findEncounter("Nope") == nullptr);
}

TEST_CASE("parseWorldmapTxt tolerates empty input", "[worldmap]") {
    CHECK(parseWorldmapTxt(std::string{}).encounters.empty());
    CHECK(parseWorldmapTxt(std::string{}).terrains.empty());
}
