#include <catch2/catch_test_macros.hpp>

#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "util/Constants.h"

using namespace geck;

TEST_CASE("Map::createEmptyElevation fills every cell with EMPTY_TILE", "[map_factory]") {
    const auto tiles = Map::createEmptyElevation();

    REQUIRE(tiles.size() == Map::TILES_PER_ELEVATION);
    for (const auto& tile : tiles) {
        CHECK(tile.getFloor() == Map::EMPTY_TILE);
        CHECK(tile.getRoof() == Map::EMPTY_TILE);
    }
}

TEST_CASE("Map::createEmptyMapFile builds a blank Fallout 2 map", "[map_factory]") {
    const auto mapFile = Map::createEmptyMapFile();

    CHECK(mapFile.header.version == static_cast<uint32_t>(FileFormat::FALLOUT2_MAP_VERSION));
    CHECK(mapFile.header.filename == "newmap");
    CHECK(mapFile.header.num_local_vars == 0);
    CHECK(mapFile.header.num_global_vars == 0);
    CHECK(mapFile.header.script_id == MapDefaults::NO_SCRIPT_ID);
    CHECK(mapFile.header.flags == MapDefaults::DEFAULT_FLAGS);

    // Three fully-tiled elevations, no objects on any of them.
    CHECK(mapFile.tiles.size() == 3);
    for (int elevation = ELEVATION_1; elevation <= ELEVATION_3; ++elevation) {
        REQUIRE(mapFile.tiles.count(elevation) == 1);
        CHECK(mapFile.tiles.at(elevation).size() == Map::TILES_PER_ELEVATION);
        REQUIRE(mapFile.map_objects.count(elevation) == 1);
        CHECK(mapFile.map_objects.at(elevation).empty());
    }

    // No scripts in any section.
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        CHECK(mapFile.map_scripts[section].empty());
        CHECK(mapFile.scripts_in_section[section] == 0);
    }
}
