#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "pattern/FillPlan.h"
#include "pattern/Pattern.h"
#include "scripting/EditArea.h"
#include "scripting/MapScriptApi.h"
#include "util/Constants.h"
#include "writer/map/MapWriter.h"

#include "support/ControllerFixture.h"

using namespace geck;
using geck::test::ControllerFixture;

namespace {
constexpr int ELEV = 0;
constexpr uint16_t SOME_TILE = 271;
constexpr uint16_t EMPTY = static_cast<uint16_t>(Map::EMPTY_TILE);

// A throwaway data tree holding just a tiles.lst, mounted so the tiles.lst-backed queries
// (tileId, tilesByPrefix) have real entries to resolve — the pattern of the frm-inspect tests.
struct TilesLstFixture {
    std::filesystem::path root;

    TilesLstFixture()
        : root(std::filesystem::temp_directory_path() / "geck_scriptapi_tiles") { // NOSONAR: throwaway test dir, not security-sensitive
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        const auto dir = root / "art/tiles";
        std::filesystem::create_directories(dir);
        std::ofstream out(dir / "tiles.lst", std::ios::binary);
        out << "edg5000.frm\nedg5001.frm\ncav1000.frm\n";
    }

    TilesLstFixture(const TilesLstFixture&) = delete;
    TilesLstFixture& operator=(const TilesLstFixture&) = delete;

    ~TilesLstFixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
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

TEST_CASE("MapScriptApi rectangle and region fills", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    SECTION("tilesInRect enumerates the inclusive rectangle, ascending") {
        const auto tiles = api.tilesInRect(5, 3, 7, 4); // 3 cols x 2 rows
        REQUIRE(tiles.size() == 6);
        CHECK(tiles.front() == api.tileIndex(5, 3));
        CHECK(tiles.back() == api.tileIndex(7, 4));
        CHECK(std::ranges::is_sorted(tiles));
    }

    SECTION("tilesInRect accepts swapped corners and clamps to the grid") {
        CHECK(api.tilesInRect(7, 4, 5, 3) == api.tilesInRect(5, 3, 7, 4));
        // A rectangle hanging off the top-left corner clamps to the on-grid part.
        const auto clamped = api.tilesInRect(-2, -2, 1, 1);
        CHECK(clamped.size() == 4);
        CHECK(clamped.front() == api.tileIndex(0, 0));
        // Fully off-grid -> empty, not an error.
        CHECK(api.tilesInRect(-5, -5, -1, -1).empty());
    }

    SECTION("fillFloorRect paints every tile in the rectangle and counts them") {
        CHECK(api.fillFloorRect(10, 10, 12, 11, SOME_TILE) == 6);
        for (const int tile : api.tilesInRect(10, 10, 12, 11)) {
            CHECK(api.getFloor(tile) == SOME_TILE);
        }
        CHECK(api.getFloorXY(13, 10) == EMPTY); // outside the rectangle untouched
        CHECK(api.paintedTiles() == 6);
    }

    SECTION("fillRoofRect paints the roof layer, not the floor") {
        CHECK(api.fillRoofRect(10, 10, 11, 10, SOME_TILE) == 2);
        CHECK(api.getRoofXY(10, 10) == SOME_TILE);
        CHECK(api.getFloorXY(10, 10) == EMPTY);
    }

    SECTION("fillRegion flood-fills the connected same-id region only") {
        // Carve a 3x3 SOME_TILE island in the empty map, with a diagonal-only neighbour
        // that must NOT be reached (the fill is 4-connected).
        REQUIRE(api.fillFloorRect(20, 20, 22, 22, SOME_TILE) == 9);
        constexpr uint16_t OTHER_TILE = 300;
        REQUIRE(api.paintFloorXY(23, 23, SOME_TILE)); // touches (22,22) diagonally only
        CHECK(api.fillRegion(21, 21, OTHER_TILE) == 9);
        for (const int tile : api.tilesInRect(20, 20, 22, 22)) {
            CHECK(api.getFloor(tile) == OTHER_TILE);
        }
        CHECK(api.getFloorXY(23, 23) == SOME_TILE); // diagonal neighbour untouched
        CHECK(api.getFloorXY(19, 20) == EMPTY);     // surrounding empty region untouched
    }

    SECTION("fillRegion is a no-op on an off-grid start or an already-matching region") {
        CHECK(api.fillRegion(-1, 0, SOME_TILE) == 0);
        REQUIRE(api.paintFloorXY(30, 30, SOME_TILE));
        CHECK(api.fillRegion(30, 30, SOME_TILE) == 0); // region already has that id
        CHECK(api.paintedTiles() == 1);
    }

    SECTION("fillRegion floods the whole empty map from any cell in one call") {
        const int total = HexagonGrid::TILE_COUNT;
        CHECK(api.fillRegion(50, 50, SOME_TILE) == total);
        CHECK(api.getFloorXY(0, 0) == SOME_TILE);
        CHECK(api.getFloorXY(99, 99) == SOME_TILE);
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

TEST_CASE("MapScriptApi newMap resets the bound map to empty", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    // Dirty the map: a tile and an object.
    REQUIRE(api.paintFloor(42, SOME_TILE));
    REQUIRE(api.placeObject(0x02000066u, 0x02000000u, 20100, 0));
    REQUIRE(api.getFloor(42) == SOME_TILE);
    REQUIRE_FALSE(fx.mapFile().map_objects[ELEV].empty());

    api.newMap();

    // Back to a fresh empty map on every elevation.
    CHECK(api.getFloor(42) == EMPTY);
    for (int e = 0; e < Map::ELEVATION_COUNT; ++e) {
        CHECK(fx.mapFile().map_objects[e].empty());
    }

    // The api still works on the fresh map — a script can keep building.
    REQUIRE(api.placeObject(0x02000066u, 0x02000000u, 20100, 0));
    CHECK(fx.mapFile().map_objects[ELEV].size() == 1);
}

TEST_CASE("MapScriptApi::mutated reports non-undoable header/map changes", "[scripting]") {
    // setPlayerStart / newMap change the map without pushing an undo command, so the host can't rely
    // on undoStackChanged to flag the map dirty — mutated() must report them.
    SECTION("a fresh api hasn't mutated the map") {
        ControllerFixture fx;
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
        CHECK_FALSE(api.mutated());
        (void)api.isValidHex(0); // a pure query is not a mutation
        CHECK_FALSE(api.mutated());
    }
    SECTION("setPlayerStart counts as a mutation") {
        ControllerFixture fx;
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
        api.setPlayerStart(api.hexIndex(50, 50), 0, 0);
        CHECK(api.mutated());
    }
    SECTION("newMap counts as a mutation") {
        ControllerFixture fx;
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
        api.newMap();
        CHECK(api.mutated());
    }
    SECTION("placing an object counts as a mutation") {
        ControllerFixture fx;
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
        REQUIRE(api.placeObject(0x02000066u, 0x02000000u, 20100, 0));
        CHECK(api.mutated());
    }
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
    // A lone scripted exit grid uses the bottom-edge marker; a world/town exit draws it in brown.
    CHECK(eg.pro_pid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(eg.frm_pid == ExitGrid::brownFrm(ExitGrid::DIR_BOTTOM));
    CHECK(eg.exit_map == ExitGrid::WORLD_MAP_EXIT); // -2 stored as 0xFFFFFFFE

    // A map-to-map exit keeps every destination field and draws the bottom marker in green.
    REQUIRE(api.placeExitGrid(20200, 5, 12345, 1, 3));
    const auto& eg2 = *objs.back();
    CHECK(eg2.pro_pid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(eg2.frm_pid == ExitGrid::greenFrm(ExitGrid::DIR_BOTTOM));
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

TEST_CASE("MapScriptApi placeExitGridRect frames a rectangle of exit grids", "[scripting]") {
    ControllerFixture fx;
    // data-only: exit-grid art isn't mounted, but the placed markers' fields are what matters.
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    const int center = api.hexIndex(100, 100);
    const int placed = api.placeExitGridRect(center, 600, 400, -2, 0, 0, 0);
    CHECK(placed > 0);
    CHECK(api.placedObjects() == placed);

    auto& objs = fx.mapFile().map_objects[ELEV];
    REQUIRE(objs.size() == static_cast<size_t>(placed));

    std::set<uint32_t> protos;
    std::set<int> hexes;
    for (const auto& o : objs) {
        CHECK(o->isExitGridMarker());                   // every marker is a MISC exit grid
        CHECK(o->exit_map == ExitGrid::WORLD_MAP_EXIT); // sharing the requested destination
        CHECK(hexes.insert(o->position).second);        // no hex placed twice (corners are shared)
        protos.insert(o->pro_pid);
    }
    // All four cardinal directional edge arts are used.
    CHECK(protos.count(ExitGrid::TOP_PRO_PID) == 1);
    CHECK(protos.count(ExitGrid::BOTTOM_PRO_PID) == 1);
    CHECK(protos.count(ExitGrid::LEFT_PRO_PID) == 1);
    CHECK(protos.count(ExitGrid::RIGHT_PRO_PID) == 1);

    // Invalid inputs raise.
    CHECK_THROWS(api.placeExitGridRect(-1, 600, 400, -2, 0, 0, 0));                        // off-grid centre
    CHECK_THROWS(api.placeExitGridRect(center, 0, 400, -2, 0, 0, 0));                      // non-positive extent
    CHECK_THROWS(api.placeExitGridRect(center, 600, 400, -2, 0, Map::ELEVATION_COUNT, 0)); // bad destElevation
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

TEST_CASE("MapScriptApi tile<->hex bridge is exact, covering, and invertible", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    SECTION("tileHexes is exactly hexTile's preimage over the whole grid") {
        // Build the preimage from hexTile, then compare every tile's tileHexes against it.
        // Counting mismatches manually keeps this a single assertion over 50k cells.
        std::vector<std::vector<int>> preimage(HexagonGrid::TILE_COUNT);
        int mapped = 0;
        for (int hex = 0; hex < HexagonGrid::POSITION_COUNT; ++hex) {
            const int tile = api.hexTile(hex);
            if (tile >= 0) {
                REQUIRE(tile < HexagonGrid::TILE_COUNT);
                preimage[tile].push_back(hex);
                ++mapped;
            }
        }
        // Nearly every hex stands on some tile (the iso projection loses a sliver at the borders).
        CHECK(mapped > HexagonGrid::POSITION_COUNT * 9 / 10);

        int mismatches = 0;
        for (int tile = 0; tile < HexagonGrid::TILE_COUNT; ++tile) {
            if (api.tileHexes(tile) != preimage[tile]) {
                ++mismatches;
            }
        }
        CHECK(mismatches == 0);
    }

    SECTION("an interior tile carries a small block of hexes, all mapping back") {
        const int tile = api.tileIndex(50, 50);
        const auto hexes = api.tileHexes(tile);
        REQUIRE(hexes.size() >= 3); // ~2x2 hexes per tile, shifted by the iso projection
        REQUIRE(hexes.size() <= 6);
        for (const int hex : hexes) {
            CHECK(api.hexTile(hex) == tile);
        }
    }

    SECTION("off-grid input is the N/A value, not an error") {
        CHECK(api.hexTile(-1) == -1);
        CHECK(api.hexTile(HexagonGrid::POSITION_COUNT) == -1);
        CHECK(api.tileHexes(-1).empty());
        CHECK(api.tileHexes(HexagonGrid::TILE_COUNT).empty());
    }
}

TEST_CASE("MapScriptApi setElevation routes edits to the chosen elevation", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    api.setElevation(2);
    REQUIRE(api.paintFloor(10, SOME_TILE));
    CHECK(api.getFloor(10) == SOME_TILE); // queries follow the switch

    api.setElevation(0);
    CHECK(api.getFloor(10) == EMPTY); // elevation 0 untouched
    CHECK(fx.map->getMapFile().tiles.at(2)[10].getFloor() == SOME_TILE);
    CHECK(fx.map->getMapFile().tiles.at(0)[10].getFloor() == EMPTY);

    CHECK_THROWS(api.setElevation(-1));
    CHECK_THROWS(api.setElevation(Map::ELEVATION_COUNT));
}

TEST_CASE("MapScriptApi hexesOnScreenRect matches the exit-grid rectangle walk", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, /*buildSprites*/ false);

    const int center = api.hexIndex(100, 100);
    const auto border = api.hexesOnScreenRect(center, 96, 48);
    REQUIRE_FALSE(border.empty());
    CHECK(std::ranges::is_sorted(border));
    CHECK(std::ranges::adjacent_find(border) == border.end()); // deduplicated

    // The same hexes placeExitGridRect walks: place a rect and compare the marker positions.
    const int placed = api.placeExitGridRect(center, 96, 48, -2, 0, 0, 0);
    CHECK(placed == static_cast<int>(border.size()));
    std::set<int> markerHexes;
    for (const auto& object : fx.map->getMapFile().map_objects.at(ELEV)) {
        markerHexes.insert(object->position);
    }
    CHECK(markerHexes == std::set<int>(border.begin(), border.end()));

    CHECK_THROWS(api.hexesOnScreenRect(-1, 96, 48));
    CHECK_THROWS(api.hexesOnScreenRect(center, 0, 48));
}

TEST_CASE("MapScriptApi reads a reference map's full floor and typed objects", "[scripting]") {
    // Author the reference with the api itself — floor paints plus exit grids (MISC markers need
    // no proto to write or parse) — then mount it at the production layout (maps/<name>.map) and
    // read it back through the reference queries. Self-contained: no proto stubs, no shipped data.
    const auto root = std::filesystem::temp_directory_path() / "geck_scriptapi_refmap"; // NOSONAR: throwaway test dir
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "maps");
    {
        ControllerFixture author;
        MapScriptApi authorApi(author.resources, author.hexgrid, author.controller, *author.map, ELEV,
            /*buildSprites*/ false);
        REQUIRE(authorApi.paintFloor(0, SOME_TILE));
        REQUIRE(authorApi.paintFloor(9999, SOME_TILE));
        REQUIRE(authorApi.placeExitGrid(20100, -2, 0, 0, 0));
        REQUIRE(authorApi.placeExitGrid(20102, -2, 0, 0, 0));
        MapWriter writer{ [](int32_t) -> Pro* { return nullptr; } };
        writer.openFile(root / "maps/ref.map");
        REQUIRE(writer.write(author.map->getMapFile()));
    }

    ControllerFixture fx;
    fx.resources.files().addDataPath(root);
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    const auto floor = api.mapFloorAt("maps/ref.map", 0);
    REQUIRE(floor.size() == Map::TILES_PER_ELEVATION);
    CHECK(floor[0] == SOME_TILE);
    CHECK(floor[9999] == SOME_TILE);
    CHECK(floor[5000] == Map::EMPTY_TILE);
    CHECK_THROWS(api.mapFloorAt("maps/ref.map", 3)); // out-of-range elevation raises

    // Objects come back through their type filter as (pid, hex, direction) triples.
    const auto misc = api.mapObjectsAt("maps/ref.map", 0, "misc");
    REQUIRE(misc.size() == 6); // two exit-grid markers
    const std::set<int> hexes{ misc[1], misc[4] };
    CHECK(hexes == std::set<int>{ 20100, 20102 });
    for (const std::size_t at : { std::size_t{ 0 }, std::size_t{ 3 } }) {
        CHECK(Pro::typeOfPid(static_cast<uint32_t>(misc[at])) == Pro::OBJECT_TYPE::MISC);
        CHECK(misc[at + 2] == 0);
    }
    CHECK(api.mapObjectsAt("maps/ref.map", 0, "wall").empty()); // no walls on this map

    CHECK_THROWS(api.mapObjectsAt("maps/ref.map", 0, "gizmo")); // unknown type name
    CHECK_THROWS(api.mapObjectsAt("no/such/map.map", 0, "wall"));

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("MapScriptApi tilesByPrefix resolves a tile family from tiles.lst", "[scripting]") {
    TilesLstFixture data;
    ControllerFixture fx;
    fx.resources.files().addDataPath(data.root.string());
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    const auto edges = api.tilesByPrefix("EDG"); // case-insensitive, like tileId
    REQUIRE(edges.size() == 2);
    CHECK(edges.at("edg5000") == 0);
    CHECK(edges.at("edg5001") == 1);
    CHECK(edges.at("edg5001") == api.tileId("edg5001")); // ids agree with tileId

    CHECK(api.tilesByPrefix("cav").at("cav1000") == 2);
    CHECK(api.tilesByPrefix("zzz").empty()); // unmatched prefix is a valid empty result
}

TEST_CASE("MapScriptApi reports genuine failures by throwing, not silently", "[scripting]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

    SECTION("data-dependent queries throw when no data is mounted (no silent empty)") {
        // These can't produce a real answer without game data, so they raise rather than hand back
        // an empty result indistinguishable from a valid "nothing here".
        CHECK_THROWS(api.tileId("edg5000"));
        CHECK_THROWS(api.tilesByPrefix("edg"));
        CHECK_THROWS(api.mapFloorAt("maps/desert1.map", 0));
        CHECK_THROWS(api.mapObjectsAt("maps/desert1.map", 0, "wall"));
        CHECK_THROWS(api.protoBlocks(0x02000066));
        CHECK_THROWS(api.protoFlat(0x02000066));
        CHECK_THROWS(api.protoFid(0x02000066));
        CHECK_THROWS(api.protoArtFrames(0x02000066));
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

TEST_CASE("The plan sink caps a run at a multiple of the bound area's footprint", "[scripting][cap]") {
    ControllerFixture fx;
    // Headless (buildSprites=false): the sink records objects with a null visual, no art needed.
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    // Footprint = 2 hexes + 1 floor tile = 3 -> cap = 8 * 3 = 24 entries per list.
    EditArea area;
    area.hexes = { 20100, 20101 };
    area.floorTiles = { 0 };
    pattern::FillPlan plan;
    api.setArea(&area);
    api.setPlanSink(&plan);

    constexpr uint32_t PRO = 0x02000066;
    constexpr uint32_t FRM = 0x02000000;

    // Objects: the 25th and later placements are refused, reported like off-grid ones.
    int placed = 0;
    for (int i = 0; i < 40; ++i) {
        if (api.placeObject(PRO, FRM, 20100, 0)) {
            ++placed;
        }
    }
    CHECK(placed == 24);
    CHECK(plan.objects.size() == 24);
    CHECK(plan.dropped == 16);
    CHECK(api.placedObjects() == 24); // refused placements are not counted as placed

    // Tiles have the same independent cap: repaints are entries too, so a runaway loop
    // repainting one tile stops growing the plan at the cap.
    int painted = 0;
    for (int i = 0; i < 40; ++i) {
        if (api.paintFloor(0, SOME_TILE)) {
            ++painted;
        }
    }
    CHECK(painted == 24);
    CHECK(plan.tiles.size() == 24);
    CHECK(plan.dropped == 16 + 16);
}

TEST_CASE("The plan sink cap trims a stamp's bulk append", "[scripting][cap]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    // Footprint = 1 hex -> cap = 8 entries per list.
    EditArea area;
    area.hexes = { 20100 };
    pattern::FillPlan plan;
    api.setArea(&area);
    api.setPlanSink(&plan);

    // A stamp with 12 objects in one variant: planInto appends all of them, then the cap
    // trims the surplus (a truncated stamp is the documented over-cap degradation).
    pattern::Pattern big;
    big.name = "big";
    pattern::PatternVariant variant;
    for (int i = 0; i < 12; ++i) {
        pattern::PatternObject object;
        object.dxHex = i; // spread along a row so every target lands on the grid
        object.proPid = 0x02000066;
        object.frmPid = 0x02000000;
        variant.objects.push_back(object);
    }
    big.variants.push_back(variant);
    api.addStamp("big", big);

    const int placed = api.placeStamp("big", 20100, 0);
    CHECK(placed == 8);
    CHECK(plan.objects.size() == 8);
    CHECK(plan.dropped == 4);
}

TEST_CASE("A sink with no bound area stays uncapped", "[scripting][cap]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    // Programmatic sink use (no fill area): the cap does not apply.
    pattern::FillPlan plan;
    api.setPlanSink(&plan);

    for (int i = 0; i < 50; ++i) {
        REQUIRE(api.placeObject(0x02000066u, 0x02000000u, 20100 + i, 0));
    }
    CHECK(plan.objects.size() == 50);
    CHECK(plan.dropped == 0);
}
