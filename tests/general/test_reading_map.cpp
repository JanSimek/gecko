#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "support/Fixtures.h"
#include "support/MapFileIo.h"

using namespace geck;

// Parses a real shipped map (sfshutl2.map) to guard the reader against
// assumptions the fully-synthetic round-trip can't exercise: real header
// values, real per-elevation framing, a real tile block, and real objects.
//
// The map references 10 distinct SCENERY protos (scenery indices 1538..1547), all
// of the GENERIC subtype, and no ITEM objects. Only ITEM/SCENERY objects
// dereference a PRO during parsing, so a stub provider supplying just those
// subtypes lets the whole map parse without the (uncommitted) game proto tree.
TEST_CASE("MapReader parses the real sfshutl2 map", "[map]") {
    const auto map = geck::test::readMapFile(geck::test::dataPath("sfshutl2.map"), geck::test::sfshutl2Provider());
    const auto& mf = map->getMapFile();

    // Header.
    CHECK(mf.header.version == 20);
    CHECK(mf.header.filename.substr(0, 12) == "SFSHUTL2.MAP");
    CHECK(mf.header.player_default_position == 20100);
    CHECK(mf.header.player_default_elevation == 0);
    // The flags mark elevations 1 and 2 as absent; only elevation 0 ships.
    CHECK(mf.header.flags == (Map::elevationFlag(1) | Map::elevationFlag(2)));

    // Objects are framed as three per-elevation blocks; only elevation 0 holds any.
    REQUIRE(mf.map_objects.at(0).size() == 227);
    CHECK(mf.map_objects.at(1).empty());
    CHECK(mf.map_objects.at(2).empty());

    // Only elevation 0's tile block is present, with a full floor+roof grid.
    CHECK(mf.tiles.count(0) == 1);
    CHECK(mf.tiles.count(1) == 0);
    CHECK(mf.tiles.count(2) == 0);
    CHECK(mf.tiles.at(0).size() == Map::TILES_PER_ELEVATION);

    // This map carries no scripts in any section.
    for (const auto& section : mf.map_scripts) {
        CHECK(section.empty());
    }

    // Every parsed object belongs to elevation 0.
    for (const auto& object : mf.map_objects.at(0)) {
        REQUIRE(object != nullptr);
        CHECK(object->elevation == 0u);
    }
}
