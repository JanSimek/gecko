#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "scripting/MapScriptApi.h"

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

TEST_CASE("MapScriptApi name resolvers fail closed without game data", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    SECTION("tileId returns -1 when the tile list is unavailable") {
        // No data mounted -> tiles.lst can't load -> unknown, not a bogus index.
        CHECK(api.tileId("edg5000") == -1);
        CHECK(api.tileId("edg5000.frm") == -1);
        CHECK(api.tileId("does-not-exist") == -1);
    }

    SECTION("the reference-map queries return empty when nothing is mounted") {
        // No data -> the reference map can't be loaded and no maps exist -> empty, not a crash.
        CHECK(api.mapScenery("maps/desert1.map").empty());
        CHECK(api.mapScenery("no/such/map.map").empty());
        CHECK(api.mapSceneryHistogram("maps/desert1.map").empty());
        CHECK(api.mapSceneryHistogram("no/such/map.map").empty());
        CHECK(api.mapFloorTiles("maps/desert1.map").empty());
        CHECK(api.listMaps().empty());
    }

    SECTION("placeProto rejects an off-grid hex before any proto lookup") {
        CHECK_FALSE(api.placeProto(0x02000066u, -1, 0));
        CHECK(api.placedObjects() == 0);
    }

    SECTION("placeProto returns false when the proto cannot be loaded") {
        // Valid hex, but headless: the proto (hence its art FID) can't resolve.
        CHECK_FALSE(api.placeProto(0x02000066u, 20100, 0));
        CHECK(api.placedObjects() == 0);
    }
}
