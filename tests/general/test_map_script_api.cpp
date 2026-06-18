#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "scripting/MapScriptApi.h"
#include "util/Constants.h"

#include "support/ControllerFixture.h"

using namespace geck;
using geck::test::ControllerFixture;

namespace {
constexpr int ELEV = 0;
constexpr uint16_t SOME_TILE = 271;
constexpr uint16_t EMPTY = static_cast<uint16_t>(Map::EMPTY_TILE);
} // namespace

TEST_CASE("MapScriptApi query surface", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    SECTION("isValidHex bounds") {
        CHECK(api.isValidHex(0));
        CHECK(api.isValidHex(HexagonGrid::POSITION_COUNT - 1));
        CHECK_FALSE(api.isValidHex(-1));
        CHECK_FALSE(api.isValidHex(HexagonGrid::POSITION_COUNT));
    }

    SECTION("an interior hex has 6 distinct on-grid neighbours") {
        const int hex = 100 * HexagonGrid::GRID_WIDTH + 100;
        const auto n = api.hexNeighbors(hex);
        REQUIRE(n.size() == 6);
        for (int h : n) {
            CHECK(api.isValidHex(h));
            CHECK(h != hex);
        }
        // All distinct.
        auto sorted = n;
        std::ranges::sort(sorted);
        CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());
    }

    SECTION("a corner hex has fewer than 6 neighbours, all on-grid") {
        const auto n = api.hexNeighbors(0);
        CHECK(n.size() < 6);
        CHECK(n.size() >= 2);
        for (int h : n) {
            CHECK(api.isValidHex(h));
        }
    }

    SECTION("empty map reads EMPTY_TILE everywhere") {
        CHECK(api.getFloor(0) == EMPTY);
        CHECK(api.getRoof(1234) == EMPTY);
        CHECK(api.getFloor(-1) == EMPTY);                      // out of range -> EMPTY
        CHECK(api.getFloor(HexagonGrid::TILE_COUNT) == EMPTY); // out of range -> EMPTY
    }
}

TEST_CASE("MapScriptApi paints tiles undoably", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    REQUIRE(api.getFloor(42) == EMPTY);
    REQUIRE(api.paintFloor(42, SOME_TILE));
    CHECK(api.getFloor(42) == SOME_TILE);
    CHECK(api.paintedTiles() == 1);

    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK(api.getFloor(42) == EMPTY); // reverted

    SECTION("roof paints land on the roof layer, not the floor") {
        REQUIRE(api.paintRoof(7, SOME_TILE));
        CHECK(api.getRoof(7) == SOME_TILE);
        CHECK(api.getFloor(7) == EMPTY);
    }

    SECTION("out-of-range tile index is rejected") {
        CHECK_FALSE(api.paintFloor(-1, SOME_TILE));
        CHECK_FALSE(api.paintFloor(HexagonGrid::TILE_COUNT, SOME_TILE));
    }
}

TEST_CASE("MapScriptApi (col,row) helpers convert and paint consistently", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    SECTION("index <-> (col,row) round-trips on both grids") {
        // position = row * width + col; hexes 200 wide, tiles 100 wide.
        CHECK(api.hexIndex(5, 3) == 3 * HexagonGrid::GRID_WIDTH + 5);
        CHECK(api.tileIndex(5, 3) == 3 * HexagonGrid::TILE_GRID_WIDTH + 5);

        const int hex = api.hexIndex(5, 3);
        CHECK(api.hexCol(hex) == 5);
        CHECK(api.hexRow(hex) == 3);

        const int tile = api.tileIndex(5, 3);
        CHECK(api.tileCol(tile) == 5);
        CHECK(api.tileRow(tile) == 3);
    }

    SECTION("off-grid (col,row) yields -1 and the XY ops no-op") {
        CHECK(api.tileIndex(HexagonGrid::TILE_GRID_WIDTH, 0) == -1);
        CHECK(api.tileIndex(-1, 0) == -1);
        CHECK(api.hexIndex(HexagonGrid::GRID_WIDTH, 0) == -1);
        CHECK(api.hexCol(-1) == -1);
        CHECK(api.hexCol(HexagonGrid::POSITION_COUNT) == -1);

        CHECK_FALSE(api.paintFloorXY(HexagonGrid::TILE_GRID_WIDTH, 0, SOME_TILE)); // off-grid -> no-op
        CHECK(api.getFloorXY(-1, -1) == EMPTY);
        CHECK(api.paintedTiles() == 0);
    }

    SECTION("paintFloorXY targets the same tile as the index form") {
        REQUIRE(api.paintFloorXY(5, 3, SOME_TILE));
        CHECK(api.getFloorXY(5, 3) == SOME_TILE);
        CHECK(api.getFloor(api.tileIndex(5, 3)) == SOME_TILE); // XY and index agree
        CHECK(api.getRoofXY(5, 3) == EMPTY);                   // floor, not roof
    }
}

TEST_CASE("MapScriptApi batches a run into one undo entry", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    REQUIRE_FALSE(fx.undoStack.canUndo());

    api.beginBatch("Procedural fill");
    for (int i = 10; i < 15; ++i) {
        REQUIRE(api.paintFloor(i, SOME_TILE));
    }
    api.endBatch();

    for (int i = 10; i < 15; ++i) {
        CHECK(api.getFloor(i) == SOME_TILE);
    }

    // The whole 5-tile run must collapse to a SINGLE undo entry: one undo reverts all
    // of it, and the stack is then empty (proving it was not 5 separate commands).
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    for (int i = 10; i < 15; ++i) {
        CHECK(api.getFloor(i) == EMPTY);
    }
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("MapScriptApi placeObject fails gracefully without loadable art", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    // Headless: no game data, so the FRM can't resolve -> no visual -> not placed.
    // The point is it returns false rather than crashing or registering a bogus object.
    CHECK_FALSE(api.placeObject(33555201u, 16777345u, 20100, 0));
    CHECK(api.placedObjects() == 0);

    // Off-grid hex is rejected before any art lookup.
    CHECK_FALSE(api.placeObject(33555201u, 16777345u, -5, 0));
}

TEST_CASE("MapScriptApi headless mode records objects as map data without GL", "[scripting]") {
    ControllerFixture fx;
    // buildSprites = false: the data-only path used by gecko-cli / CI.
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    constexpr uint32_t PRO = 0x02000066; // Scrub (scenery)
    constexpr uint32_t FRM = 0x02000000; // arbitrary FID; not resolved in data-only mode
    constexpr int HEX = 20100;

    // No game data and no GL context, yet placement succeeds: it only writes the .map fields.
    REQUIRE(api.placeObject(PRO, FRM, HEX, 0));
    CHECK(api.placedObjects() == 1);

    auto& objs = fx.mapFile().map_objects[ELEV];
    REQUIRE(objs.size() == 1);
    CHECK(objs[0]->pro_pid == PRO);
    CHECK(objs[0]->frm_pid == FRM);
    CHECK(objs[0]->position == HEX);
    CHECK(fx.objects.empty()); // no sprite was built

    // Off-grid is still rejected before any data is written.
    CHECK_FALSE(api.placeObject(PRO, FRM, -1, 0));
    CHECK(api.placedObjects() == 1);

    // The placement is one undo entry.
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK(fx.mapFile().map_objects[ELEV].empty());
}

TEST_CASE("MapScriptApi setPlayerStart writes the map header", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    const int hex = api.hexIndex(99, 99);
    api.setPlayerStart(hex, 2, 0);
    const auto& header = fx.mapFile().header;
    CHECK(header.player_default_position == static_cast<uint32_t>(hex));
    CHECK(header.player_default_orientation == 2u);
    CHECK(header.player_default_elevation == 0u);

    // Out-of-range values raise rather than write a bogus header.
    CHECK_THROWS(api.setPlayerStart(-1, 0, 0));                     // off-grid hex
    CHECK_THROWS(api.setPlayerStart(hex, 6, 0));                    // orientation > 5
    CHECK_THROWS(api.setPlayerStart(hex, 0, Map::ELEVATION_COUNT)); // elevation out of range
}

TEST_CASE("MapScriptApi placeExitGrid records a MISC exit-grid object", "[scripting]") {
    ControllerFixture fx;
    // data-only: exit-grid art isn't mounted, but the .map fields are what matters here.
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    constexpr int HEX = 20100;
    // A worldmap exit (destMapId -2): the engine ignores destHex for it.
    REQUIRE(api.placeExitGrid(HEX, -2, 0, 0, 0));
    CHECK(api.placedObjects() == 1);

    auto& objs = fx.mapFile().map_objects[ELEV];
    REQUIRE(objs.size() == 1);
    const auto& eg = *objs.front();
    CHECK(eg.position == HEX);
    CHECK(eg.isExitGridMarker()); // MISC type 0x05, proto index 16..23
    CHECK(eg.pro_pid == ExitGrid::WORLD_EXIT_PRO_PID);
    CHECK(eg.exit_map == ExitGrid::WORLD_MAP_EXIT); // -2 stored as 0xFFFFFFFE

    // A map-to-map exit uses the map-exit proto and keeps every destination field.
    REQUIRE(api.placeExitGrid(20200, 5, 12345, 1, 3));
    const auto& eg2 = *objs.back();
    CHECK(eg2.pro_pid == ExitGrid::MAP_EXIT_PRO_PID);
    CHECK(eg2.exit_map == 5u);
    CHECK(eg2.exit_position == 12345u);
    CHECK(eg2.exit_elevation == 1u);
    CHECK(eg2.exit_orientation == 3u);

    // Invalid inputs raise (not a silently-dropped or bogus object).
    CHECK_THROWS(api.placeExitGrid(-1, 0, 0, 0, 0));                     // off-grid placement hex
    CHECK_THROWS(api.placeExitGrid(HEX, -3, 0, 0, 0));                   // bad destMapId
    CHECK_THROWS(api.placeExitGrid(HEX, 0, -1, 0, 0));                   // destHex out of range
    CHECK_THROWS(api.placeExitGrid(HEX, 0, 0, Map::ELEVATION_COUNT, 0)); // destElevation out of range
    CHECK_THROWS(api.placeExitGrid(HEX, 0, 0, 0, 6));                    // orientation > 5
    CHECK(api.placedObjects() == 2);                                     // the failed calls added nothing
}

TEST_CASE("MapScriptApi::proto builds a PID from a readable type name and id", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    // Pure (type << 24) | id; produces exactly the hex a script would otherwise hand-write.
    CHECK(api.proto("scenery", 102) == 0x02000066u); // Scrub
    CHECK(api.proto("item", 1) == 0x00000001u);
    CHECK(api.proto("critter", 1) == 0x01000001u);
    CHECK(api.proto("misc", 16) == 0x05000010u); // exit-grid range is MISC 16..23

    // Case-insensitive, singular or plural.
    CHECK(api.proto("Scenery", 102) == api.proto("scenery", 102));
    CHECK(api.proto("items", 1) == api.proto("item", 1));

    // Raises on an unknown type or an out-of-range id (not a silent bogus PID).
    CHECK_THROWS(api.proto("vehicle", 1));
    CHECK_THROWS(api.proto("scenery", 0));
    CHECK_THROWS(api.proto("scenery", -1));
    CHECK_THROWS(api.proto("scenery", 0x1000000));
}

TEST_CASE("MapScriptApi reports genuine failures by throwing, not silently", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    SECTION("data-dependent queries throw when no data is mounted (no silent empty)") {
        // These can't produce a real answer without game data, so they raise rather than hand back
        // an empty result indistinguishable from a valid "nothing here".
        CHECK_THROWS(api.tileId("edg5000"));
        CHECK_THROWS(api.mapScenery("maps/desert1.map"));
        CHECK_THROWS(api.mapScenery("no/such/map.map"));
        CHECK_THROWS(api.mapSceneryHistogram("maps/desert1.map"));
        CHECK_THROWS(api.mapFloorTiles("maps/desert1.map"));
        CHECK_THROWS(api.protoName(0x02000066));
    }

    SECTION("listMaps is a graceful enumeration -> empty (no maps is a valid answer)") {
        CHECK(api.listMaps().empty());
    }

    SECTION("placeProto stays a status return (skip), not a throw") {
        CHECK_FALSE(api.placeProto(0x02000066u, -1, 0));    // off-grid hex
        CHECK_FALSE(api.placeProto(0x02000066u, 20100, 0)); // valid hex, but the proto art can't load
        CHECK(api.placedObjects() == 0);
    }

    SECTION("noise2d is pure: in [0,1] and deterministic") {
        for (int xi = 0; xi < 4; ++xi) {
            for (int yi = 0; yi < 4; ++yi) {
                const double x = xi * 1.3;
                const double y = yi * 1.7;
                const double n = api.noise2d(x, y);
                CHECK(n >= 0.0);
                CHECK(n <= 1.0);
                CHECK(api.noise2d(x, y) == n); // same input -> same output
            }
        }
        CHECK(api.noise2d(1.5, 2.5) != api.noise2d(40.5, 80.5)); // varies across the field
    }
}
