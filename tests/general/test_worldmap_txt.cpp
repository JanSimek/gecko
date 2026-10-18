#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
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
Frequent=43%       ; without None the engine reads no frequency at all
terrain_types=Desert:1, Mountain:2, City:1, Ocean:1
terrain_short_names=DES, MNT, CTY, OCN ; WIP

[Random Maps: Desert]   ; not parsed
map_00=desert1

[Encounter: Raiders]  ; WIP!
type_00=Ratio:35%, pid:16777252, Item:8{Wielded}, Item:40, Script:255, Distance:7
type_01=Dead, pid:16777220, Item:40, Script:256

[Encounter: ARRO_Rats]
type_00=ratio:100%, pid:16777276, Distance:3
)";
} // namespace

TEST_CASE("parseWorldmapTxt reads terrain types and encounter groups", "[worldmap_txt]") {
    const WorldmapTxt world = parseWorldmapTxt(std::string{ kWorldmapTxt });

    REQUIRE(world.terrains.size() == 4);
    CHECK(world.terrains[0].name == "Desert");
    CHECK(world.terrains[0].shortName == "DES");
    CHECK(world.terrains[0].difficulty == 1);
    CHECK(world.terrains[1].name == "Mountain");
    CHECK(world.terrains[1].difficulty == 2);
    CHECK(world.terrains[3].shortName == "OCN");

    REQUIRE(world.encounters.size() == 2);
    const Encounter* raiders = world.findEncounter("Raiders");
    REQUIRE(raiders != nullptr);
    REQUIRE(raiders->entries.size() == 2);
    CHECK(raiders->entries[0].pid == 16777252);
    CHECK(raiders->entries[0].ratioPercent == 35); // "35%" parsed
    CHECK(raiders->entries[0].dead == false);
    CHECK(raiders->entries[0].script == 255);
    CHECK(raiders->entries[0].distance == 7);                      // Distance:NN branch
    CHECK(raiders->entries[0].items == std::vector<int>{ 8, 40 }); // "8{Wielded}" -> 8
    CHECK(raiders->entries[1].dead == true);                       // bare "Dead" flag
    CHECK(raiders->entries[1].pid == 16777220);
    CHECK(raiders->entries[1].distance == -1); // absent -> default

    const Encounter* rats = world.findEncounter("ARRO_Rats");
    REQUIRE(rats != nullptr);
    REQUIRE(rats->entries.size() == 1);
    CHECK(rats->entries[0].pid == 16777276);
    CHECK(rats->entries[0].ratioPercent == 100); // lowercase "ratio:" key still parsed
    CHECK(rats->entries[0].distance == 3);

    CHECK(world.findEncounter("Nope") == nullptr);
    CHECK(world.frequencyPercent == std::array<int, 6>{});
}

TEST_CASE("parseWorldmapTxt reads the [Tile NN] grid and terrainAt maps pixels to terrain", "[worldmap_txt]") {
    constexpr const char* kTiles = R"(
[Data]
terrain_types=Desert:1, Mountain:4, Ocean:2
[Tile Data]
num_horizontal_tiles=2
[Tile 0]
art_idx=339
encounter_difficulty=0
walk_mask_name=wrldmp00
0_0=Desert, No_Fill, None, None, None, Desert1
1_0=Mountain, No_Fill, None, None, None, Mtn
0_1=Ocean, Fill_W, None, None, None, Fish
[Tile 1]
art_idx=340
encounter_difficulty=3
0_0=Mountain, No_Fill, None, None, None, Mtn
)";
    const WorldmapTxt world = parseWorldmapTxt(std::string{ kTiles });

    CHECK(world.numHorizontalTiles == 2);
    REQUIRE(world.tiles.size() == 2);

    // art_idx is what the background is made of: an intrface.lst index, resolved as a FID with the
    // OBJ_TYPE_INTERFACE type byte (the shipped tiles are art/intrface/wrldmp00.frm upwards).
    CHECK(world.tiles[0].artIndex == 339);
    CHECK(world.tiles[0].walkMaskName == "wrldmp00");
    CHECK(world.tiles[0].encounterDifficulty == 0);
    CHECK(world.tiles[1].artIndex == 340);
    CHECK(world.tiles[1].encounterDifficulty == 3);
    CHECK(world.tiles[1].walkMaskName.empty()); // no mask = walkable throughout

    // A 2x1 grid of 350x300 tiles.
    CHECK(world.numVerticalTiles() == 1);
    CHECK(world.widthPixels() == 700);
    CHECK(world.heightPixels() == 300);
    // terrainAt: row = (x%350)/50, col = (y%300)/50, tile = (y/300)*numHoriz + x/350.
    CHECK(world.terrainAt(0, 0) == "Desert");     // tile 0, row 0, col 0
    CHECK(world.terrainAt(60, 0) == "Mountain");  // row 1 (60/50)
    CHECK(world.terrainAt(0, 60) == "Ocean");     // col 1 (60/50)
    CHECK(world.terrainAt(350, 0) == "Mountain"); // tile 1
    CHECK(world.terrainAt(99999, 0).empty());     // out of range

    CHECK(world.difficultyOf("Mountain") == 4);
    CHECK(world.difficultyOf("Nope") == 0);

    // The rest of a sub-tile line: fill, the frequency per part of the day, and the encounter table.
    const WorldmapSubtile* ocean = world.subtileAt(0, 60);
    REQUIRE(ocean != nullptr);
    CHECK(ocean->fill == "fill_w");
    CHECK(ocean->chance[0] == EncounterFrequency::None);
    CHECK(ocean->encounterTable == "Fish");
    CHECK(world.subtileAt(-1, 0) == nullptr);
}

TEST_CASE("parseWorldmapTxt reads encounter frequencies and tables as the engine does", "[worldmap_txt]") {
    constexpr const char* kTables = R"(
[Data]
Forced=100%
Common=22%
None=0%
Rare=4%
Uncommon=12%
; no Frequent: the engine stops there and leaves Forced 0 too

[Encounter Table 0]
lookup_name=DstN
maps=Desert Encounter 1, Desert Encounter 2
enc_00=Chance:50%,Enc:(2-5) Raiders AMBUSH Player,Scenery:Normal,If(Player(Level) > 4)
enc_01=Chance:9%,Enc:(2-4) ARRO_Spore_Plants AND (1-2) ARRO_Silver_Geckos FIGHTING Player
enc_02=Chance:1%,Special,If(Global(1) > 1) And If(Player(Level) < 7)   ; trailing comment
enc_03=Chance:5%,Counter:1,Map:Special Holy Encounter 2,Enc:Special1,Scenery:Heavy
enc_05=Chance:5%,Enc:(1-1) Unreachable   ; enc_04 is missing, so the engine stops before this

[Encounter Table 2]
lookup_name=Orphan   ; Table 1 is missing, so the engine never reads this

[Tile Data]
num_horizontal_tiles=1
[Tile 0]
art_idx=339
0_0=Desert, No_Fill, Common, Uncommon, Rare, dstn
)";
    const WorldmapTxt world = parseWorldmapTxt(std::string{ kTables });

    CHECK(world.frequencyPercent == std::array<int, 6>{ 0, 4, 12, 22, 0, 0 });

    REQUIRE(world.encounterTables.size() == 1);
    const EncounterTable& table = world.encounterTables[0];
    CHECK(table.lookupName == "DstN");
    CHECK(table.maps == std::vector<std::string>{ "Desert Encounter 1", "Desert Encounter 2" });
    REQUIRE(table.entries.size() == 4);
    CHECK(table.skippedEntries == 1); // enc_05

    const EncounterTableEntry& raiders = table.entries[0];
    CHECK(raiders.chance == 50);
    CHECK(raiders.counter == -1);
    CHECK(raiders.condition == "If(Player(Level) > 4)");
    REQUIRE(raiders.groups.size() == 2);
    CHECK(raiders.groups[0].name == "Raiders");
    CHECK(raiders.groups[0].minCount == 2);
    CHECK(raiders.groups[0].maxCount == 5);
    CHECK(raiders.groups[0].situation == "ambush");
    CHECK(raiders.groups[1].name == "Player");
    CHECK(raiders.groups[1].minCount == 1);
    CHECK(raiders.groups[1].situation == "nothing");

    const auto& fight = table.entries[1].groups;
    REQUIRE(fight.size() == 3);
    CHECK(fight[0].situation == "and");
    CHECK(fight[1].name == "ARRO_Silver_Geckos");
    CHECK(fight[1].situation == "fighting");

    CHECK(table.entries[2].special);
    CHECK(table.entries[2].groups.empty());
    CHECK(table.entries[2].condition == "If(Global(1) > 1) And If(Player(Level) < 7)");

    const EncounterTableEntry& holy = table.entries[3];
    CHECK(holy.counter == 1);
    CHECK(holy.map == "Special Holy Encounter 2");
    CHECK(holy.scenery == "heavy");
    REQUIRE(holy.groups.size() == 1);
    CHECK(holy.groups[0].name == "Special1");

    // A sub-tile names its table case-insensitively, as wmParseFindEncounterTypeMatch matches it.
    const WorldmapSubtile* desert = world.subtileAt(10, 10);
    REQUIRE(desert != nullptr);
    CHECK(desert->chance == std::array{ EncounterFrequency::Common, EncounterFrequency::Uncommon, EncounterFrequency::Rare });
    CHECK(world.findEncounterTable(desert->encounterTable) == &table);
    CHECK(world.findEncounterTable("Orphan") == nullptr);
}

TEST_CASE("parseWorldmapTxt tolerates empty input", "[worldmap_txt]") {
    CHECK(parseWorldmapTxt(std::string{}).encounters.empty());
    CHECK(parseWorldmapTxt(std::string{}).terrains.empty());
    CHECK(parseWorldmapTxt(std::string{}).terrainAt(0, 0).empty());
}
