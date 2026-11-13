#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <set>

#include "editor/HexagonGrid.h"
#include "format/frm/FrmId.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "format/pro/ProtoTypes.h"
#include "pattern/FillPlan.h"
#include "pattern/Pattern.h"
#include "scripting/EditArea.h"
#include "scripting/MapScriptApi.h"
#include "util/Constants.h"

#include "support/ByteWriter.h"
#include "support/MapFileIo.h"
#include "support/ProBuilder.h"
#include "support/ScriptApiFixture.h"
#include "support/QuiltReference.h"
#include "support/TempFile.h"

using namespace geck;
using geck::test::ScriptApiFixture;
namespace fs = std::filesystem;

namespace {
constexpr int ELEV = ScriptApiFixture::ELEVATION;
constexpr uint16_t SOME_TILE = 271;
constexpr uint16_t EMPTY = static_cast<uint16_t>(Map::EMPTY_TILE);

} // namespace

TEST_CASE("MapScriptApi answers hex validity, neighbour and tile queries on an empty map", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    SECTION("isValidHex bounds") {
        CHECK(fx.api.isValidHex(0));
        CHECK(fx.api.isValidHex(HexagonGrid::POSITION_COUNT - 1));
        CHECK_FALSE(fx.api.isValidHex(-1));
        CHECK_FALSE(fx.api.isValidHex(HexagonGrid::POSITION_COUNT));
    }

    SECTION("an interior hex has 6 distinct on-grid neighbours") {
        const int hex = 100 * HexagonGrid::GRID_WIDTH + 100;
        const auto n = fx.api.hexNeighbors(hex);
        REQUIRE(n.size() == 6);
        for (int h : n) {
            CHECK(fx.api.isValidHex(h));
            CHECK(h != hex);
        }
        // All distinct.
        auto sorted = n;
        std::ranges::sort(sorted);
        CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());
    }

    SECTION("a corner hex has fewer than 6 neighbours, all on-grid") {
        const auto n = fx.api.hexNeighbors(0);
        CHECK(n.size() < 6);
        CHECK(n.size() >= 2);
        for (int h : n) {
            CHECK(fx.api.isValidHex(h));
        }
    }

    SECTION("empty map reads EMPTY_TILE everywhere") {
        CHECK(fx.api.getFloor(0) == EMPTY);
        CHECK(fx.api.getRoof(1234) == EMPTY);
        CHECK(fx.api.getFloor(-1) == EMPTY);                      // out of range -> EMPTY
        CHECK(fx.api.getFloor(HexagonGrid::TILE_COUNT) == EMPTY); // out of range -> EMPTY
    }
}

TEST_CASE("MapScriptApi paints tiles undoably", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    REQUIRE(fx.api.getFloor(42) == EMPTY);
    REQUIRE(fx.api.paintFloor(42, SOME_TILE));
    CHECK(fx.api.getFloor(42) == SOME_TILE);
    CHECK(fx.api.paintedTiles() == 1);

    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK(fx.api.getFloor(42) == EMPTY); // reverted

    SECTION("roof paints land on the roof layer, not the floor") {
        REQUIRE(fx.api.paintRoof(7, SOME_TILE));
        CHECK(fx.api.getRoof(7) == SOME_TILE);
        CHECK(fx.api.getFloor(7) == EMPTY);
    }

    SECTION("out-of-range tile index is rejected") {
        CHECK_FALSE(fx.api.paintFloor(-1, SOME_TILE));
        CHECK_FALSE(fx.api.paintFloor(HexagonGrid::TILE_COUNT, SOME_TILE));
    }
}

TEST_CASE("MapScriptApi (col,row) helpers convert and paint consistently", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    SECTION("index <-> (col,row) round-trips on both grids") {
        // position = row * width + col; hexes 200 wide, tiles 100 wide.
        CHECK(fx.api.hexIndex(5, 3) == 3 * HexagonGrid::GRID_WIDTH + 5);
        CHECK(fx.api.tileIndex(5, 3) == 3 * HexagonGrid::TILE_GRID_WIDTH + 5);

        const int hex = fx.api.hexIndex(5, 3);
        CHECK(fx.api.hexCol(hex) == 5);
        CHECK(fx.api.hexRow(hex) == 3);

        const int tile = fx.api.tileIndex(5, 3);
        CHECK(fx.api.tileCol(tile) == 5);
        CHECK(fx.api.tileRow(tile) == 3);
    }

    SECTION("off-grid (col,row) yields -1 and the XY ops no-op") {
        CHECK(fx.api.tileIndex(HexagonGrid::TILE_GRID_WIDTH, 0) == -1);
        CHECK(fx.api.tileIndex(-1, 0) == -1);
        CHECK(fx.api.hexIndex(HexagonGrid::GRID_WIDTH, 0) == -1);
        CHECK(fx.api.hexCol(-1) == -1);
        CHECK(fx.api.hexCol(HexagonGrid::POSITION_COUNT) == -1);

        CHECK_FALSE(fx.api.paintFloorXY(HexagonGrid::TILE_GRID_WIDTH, 0, SOME_TILE)); // off-grid -> no-op
        CHECK(fx.api.getFloorXY(-1, -1) == EMPTY);
        CHECK(fx.api.paintedTiles() == 0);
    }

    SECTION("paintFloorXY targets the same tile as the index form") {
        REQUIRE(fx.api.paintFloorXY(5, 3, SOME_TILE));
        CHECK(fx.api.getFloorXY(5, 3) == SOME_TILE);
        CHECK(fx.api.getFloor(fx.api.tileIndex(5, 3)) == SOME_TILE); // XY and index agree
        CHECK(fx.api.getRoofXY(5, 3) == EMPTY);                      // floor, not roof
    }
}

TEST_CASE("MapScriptApi fills rectangles and flood-fills connected regions", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    SECTION("tilesInRect enumerates the inclusive rectangle, ascending") {
        const auto tiles = fx.api.tilesInRect(5, 3, 7, 4); // 3 cols x 2 rows
        REQUIRE(tiles.size() == 6);
        CHECK(tiles.front() == fx.api.tileIndex(5, 3));
        CHECK(tiles.back() == fx.api.tileIndex(7, 4));
        CHECK(std::ranges::is_sorted(tiles));
    }

    SECTION("tilesInRect accepts swapped corners and clamps to the grid") {
        CHECK(fx.api.tilesInRect(7, 4, 5, 3) == fx.api.tilesInRect(5, 3, 7, 4));
        // A rectangle hanging off the top-left corner clamps to the on-grid part.
        const auto clamped = fx.api.tilesInRect(-2, -2, 1, 1);
        REQUIRE(clamped.size() == 4);
        CHECK(clamped.front() == fx.api.tileIndex(0, 0));
        // Fully off-grid -> empty, not an error.
        CHECK(fx.api.tilesInRect(-5, -5, -1, -1).empty());
    }

    SECTION("fillFloorRect paints every tile in the rectangle and counts them") {
        CHECK(fx.api.fillFloorRect(10, 10, 12, 11, SOME_TILE) == 6);
        for (const int tile : fx.api.tilesInRect(10, 10, 12, 11)) {
            CHECK(fx.api.getFloor(tile) == SOME_TILE);
        }
        CHECK(fx.api.getFloorXY(13, 10) == EMPTY); // outside the rectangle untouched
        CHECK(fx.api.paintedTiles() == 6);
    }

    SECTION("fillRoofRect paints the roof layer, not the floor") {
        CHECK(fx.api.fillRoofRect(10, 10, 11, 10, SOME_TILE) == 2);
        CHECK(fx.api.getRoofXY(10, 10) == SOME_TILE);
        CHECK(fx.api.getFloorXY(10, 10) == EMPTY);
    }

    SECTION("fillRegion flood-fills the connected same-id region only") {
        // Carve a 3x3 SOME_TILE island in the empty map, with a diagonal-only neighbour
        // that must NOT be reached (the fill is 4-connected).
        REQUIRE(fx.api.fillFloorRect(20, 20, 22, 22, SOME_TILE) == 9);
        constexpr uint16_t OTHER_TILE = 300;
        REQUIRE(fx.api.paintFloorXY(23, 23, SOME_TILE)); // touches (22,22) diagonally only
        CHECK(fx.api.fillRegion(21, 21, OTHER_TILE) == 9);
        for (const int tile : fx.api.tilesInRect(20, 20, 22, 22)) {
            CHECK(fx.api.getFloor(tile) == OTHER_TILE);
        }
        CHECK(fx.api.getFloorXY(23, 23) == SOME_TILE); // diagonal neighbour untouched
        CHECK(fx.api.getFloorXY(19, 20) == EMPTY);     // surrounding empty region untouched
    }

    SECTION("fillRegion is a no-op on an off-grid start or an already-matching region") {
        CHECK(fx.api.fillRegion(-1, 0, SOME_TILE) == 0);
        REQUIRE(fx.api.paintFloorXY(30, 30, SOME_TILE));
        CHECK(fx.api.fillRegion(30, 30, SOME_TILE) == 0); // region already has that id
        CHECK(fx.api.paintedTiles() == 1);
    }

    SECTION("fillRegion floods the whole empty map from any cell in one call") {
        const int total = HexagonGrid::TILE_COUNT;
        CHECK(fx.api.fillRegion(50, 50, SOME_TILE) == total);
        CHECK(fx.api.getFloorXY(0, 0) == SOME_TILE);
        CHECK(fx.api.getFloorXY(99, 99) == SOME_TILE);
    }
}

TEST_CASE("MapScriptApi batches a run into one undo entry", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    REQUIRE_FALSE(fx.undoStack.canUndo());

    {
        ScopedUndoBatch batch(fx.controller, "Procedural fill");
        for (int i = 10; i < 15; ++i) {
            REQUIRE(fx.api.paintFloor(i, SOME_TILE));
        }
    }

    for (int i = 10; i < 15; ++i) {
        CHECK(fx.api.getFloor(i) == SOME_TILE);
    }

    // The whole 5-tile run must collapse to a SINGLE undo entry: one undo reverts all
    // of it, and the stack is then empty (proving it was not 5 separate commands).
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    for (int i = 10; i < 15; ++i) {
        CHECK(fx.api.getFloor(i) == EMPTY);
    }
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("MapScriptApi placeObject fails gracefully without loadable art", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    // Headless: no game data, so the FRM can't resolve -> no visual -> not placed.
    // The point is it returns false rather than crashing or registering a bogus object.
    CHECK_FALSE(fx.api.placeObject(ProtoId(ObjectType::Scenery, 769).pid(), FrmId(ObjectType::Critter, 129).fid(), 20100, 0));
    CHECK(fx.api.placedObjects() == 0);

    // Off-grid hex is rejected before any art lookup.
    CHECK_FALSE(fx.api.placeObject(ProtoId(ObjectType::Scenery, 769).pid(), FrmId(ObjectType::Critter, 129).fid(), -5, 0));
}

TEST_CASE("MapScriptApi headless mode records objects as map data without GL", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };
    // buildSprites = false: the data-only path used by gecko-cli / CI.

    constexpr uint32_t PRO = 0x02000066; // Scrub (scenery)
    constexpr uint32_t FRM = 0x02000000; // arbitrary FID; not resolved in data-only mode
    constexpr int HEX = 20100;

    // No game data and no GL context, yet placement succeeds: it only writes the .map fields.
    REQUIRE(fx.api.placeObject(PRO, FRM, HEX, 0));
    CHECK(fx.api.placedObjects() == 1);

    auto& objs = fx.mapFile().map_objects[ELEV];
    REQUIRE(objs.size() == 1);
    CHECK(objs[0]->pro_pid == PRO);
    CHECK(objs[0]->frm_pid == FRM);
    CHECK(objs[0]->position == HEX);
    CHECK(fx.objects.empty()); // no sprite was built

    // Off-grid is still rejected before any data is written.
    CHECK_FALSE(fx.api.placeObject(PRO, FRM, -1, 0));
    CHECK(fx.api.placedObjects() == 1);

    // The placement is one undo entry.
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK(fx.mapFile().map_objects[ELEV].empty());
}

namespace {
// A data tree with two wall protos: 1 names a script (an item-type SID, program index 68, the way
// the shipped gravesite protos do) and 2 names none. Both are flat and NoBlock.
constexpr uint32_t SCRIPTED_WALL = 0x03000001;
constexpr uint32_t PLAIN_WALL = 0x03000002;
constexpr uint32_t PROTO_SID = 0x03000044;
constexpr auto WALL_FLAGS = static_cast<uint32_t>(Pro::ObjectFlags::OBJECT_FLAT)
    | static_cast<uint32_t>(Pro::ObjectFlags::OBJECT_NO_BLOCK);

void writeWallProtos(const std::filesystem::path& root) {
    test::writeFile(root / "proto" / "walls" / "walls.lst", "scripted.pro\nplain.pro\n");
    const auto write = [&root](const char* file, uint32_t pid, uint32_t sid) {
        Pro pro{ root / "proto" / "walls" / file };
        pro.header.PID = static_cast<int32_t>(pid);
        pro.header.FID = 0x03000000;
        pro.header.flags = WALL_FLAGS;
        pro.commonItemData.SID = sid;
        test::writePro(root / "proto" / "walls" / file, pro);
    };
    write("scripted.pro", SCRIPTED_WALL, PROTO_SID);
    write("plain.pro", PLAIN_WALL, MapScript::NONE);
}

constexpr int ITEM_SECTION = static_cast<int>(MapScript::ScriptType::ITEM);
} // namespace

// The engine's obj_new seeds a new object from its proto: the flags (the engine reads them from the
// map, so flags = 0 made a flat proto stand and a NoBlock one block) and the script the proto names
// (objectSetScriptFromProto), so a placed gravesite runs zigengrv. Placement used to write neither.
TEST_CASE("MapScriptApi placement seeds a new object's flags and script from its proto", "[map_script_api][scripting]") {
    const test::TempDir rootDir{ "geck_place_proto_script" };
    const fs::path& root = rootDir.path();
    writeWallProtos(root);

    ScriptApiFixture fx{ false };
    fx.resources.files().addDataPath(root.string());

    SECTION("placeProto and placeObject both carry the proto's flags") {
        REQUIRE(fx.api.placeProto(PLAIN_WALL, 20100, 0));
        REQUIRE(fx.api.placeObject(PLAIN_WALL, 0x03000000, 20101, 0));
        const auto& objs = fx.mapFile().map_objects[ELEV];
        REQUIRE(objs.size() == 2);
        CHECK(objs[0]->flags == WALL_FLAGS);
        CHECK(objs[1]->flags == WALL_FLAGS);
    }

    SECTION("a scripted proto's script is attached in the same undo entry") {
        REQUIRE(fx.api.placeProto(SCRIPTED_WALL, 20100, 0));
        REQUIRE(fx.api.placeProto(PLAIN_WALL, 20101, 0));
        const auto& objs = fx.mapFile().map_objects[ELEV];
        REQUIRE(objs.size() == 2);
        const auto& scripts = fx.mapFile().map_scripts[ITEM_SECTION];
        REQUIRE(scripts.size() == 1);
        CHECK(fx.mapFile().scripts_in_section[ITEM_SECTION] == 1);
        CHECK(scripts[0].script_id == 68); // the SID's low 24 bits: the 0-based program index
        CHECK(MapScript::sidSection(scripts[0].pid) == ITEM_SECTION);
        CHECK(objs[0]->map_scripts_pid == static_cast<int32_t>(scripts[0].pid));
        CHECK(scripts[0].script_oid == objs[0]->unknown0);
        CHECK(objs[1]->map_scripts_pid == -1);

        // Object and script are one undo entry each placement: undoing the scripted one removes both.
        REQUIRE(fx.undoStack.undo()); // the plain wall
        REQUIRE(fx.undoStack.undo());
        CHECK(fx.mapFile().map_objects[ELEV].empty());
        CHECK(fx.mapFile().map_scripts[ITEM_SECTION].empty());
        CHECK(fx.mapFile().scripts_in_section[ITEM_SECTION] == 0);
        REQUIRE(fx.undoStack.redo());
        REQUIRE(fx.mapFile().map_objects[ELEV].size() == 1);
        CHECK(fx.mapFile().map_scripts[ITEM_SECTION].size() == 1);
    }
}

// copyObjects follows the engine's _obj_copy: a copy of a scripted object gets its proto's script
// (never the source's instance script, which belongs to the source map), an unscripted one none.
TEST_CASE("MapScriptApi copyObjects re-attaches the proto script only to copies of scripted objects", "[map_script_api][scripting]") {
    const test::TempDir rootDir{ "geck_copy_proto_script" };
    const fs::path& root = rootDir.path();
    writeWallProtos(root);
    {
        ScriptApiFixture author{ false };
        author.resources.files().addDataPath(root.string());
        REQUIRE(author.api.placeProto(SCRIPTED_WALL, 20100, 0)); // gets the proto's script
        REQUIRE(author.api.placeProto(SCRIPTED_WALL, 20101, 0));
        REQUIRE(author.controller.detachScript(author.mapFile().map_objects[ELEV][1])); // a mapper removed it
        REQUIRE(author.controller.attachScript(author.mapFile().map_objects[ELEV][0], ITEM_SECTION, 7));
        test::writeMapFile(author.mapFile(), root / "maps/ref.map");
    }

    ScriptApiFixture fx{ false };
    fx.resources.files().addDataPath(root.string());
    REQUIRE(fx.api.copyObjects("maps/ref.map", ELEV, { "wall" }, {}) == 2);

    const auto& objs = fx.mapFile().map_objects[ELEV];
    REQUIRE(objs.size() == 2);
    const auto& scripts = fx.mapFile().map_scripts[ITEM_SECTION];
    REQUIRE(scripts.size() == 1);
    CHECK(scripts[0].script_id == 68); // the proto's, not the source instance's 7
    CHECK(objs[0]->map_scripts_pid == static_cast<int32_t>(scripts[0].pid));
    CHECK(objs[1]->map_scripts_pid == -1);
}

TEST_CASE("MapScriptApi newMap resets the bound map to empty", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };

    // Dirty the map: a tile and an object.
    REQUIRE(fx.api.paintFloor(42, SOME_TILE));
    REQUIRE(fx.api.placeObject(0x02000066u, 0x02000000u, 20100, 0));
    REQUIRE(fx.api.getFloor(42) == SOME_TILE);
    REQUIRE_FALSE(fx.mapFile().map_objects[ELEV].empty());

    fx.api.newMap();

    // Back to a fresh empty map on every elevation.
    CHECK(fx.api.getFloor(42) == EMPTY);
    for (int e = 0; e < Map::ELEVATION_COUNT; ++e) {
        CHECK(fx.mapFile().map_objects[e].empty());
    }

    // The api still works on the fresh map — a script can keep building.
    REQUIRE(fx.api.placeObject(0x02000066u, 0x02000000u, 20100, 0));
    CHECK(fx.mapFile().map_objects[ELEV].size() == 1);
}

TEST_CASE("MapScriptApi::mutated reports non-undoable header/map changes", "[map_script_api][scripting]") {
    // setPlayerStart / newMap change the map without pushing an undo command, so the host can't rely
    // on undoStackChanged to flag the map dirty — mutated() must report them.
    SECTION("a fresh api hasn't mutated the map") {
        ScriptApiFixture fx{ false };
        CHECK_FALSE(fx.api.mutated());
        (void)fx.api.isValidHex(0); // a pure query is not a mutation
        CHECK_FALSE(fx.api.mutated());
    }
    SECTION("setPlayerStart counts as a mutation") {
        ScriptApiFixture fx{ false };
        fx.api.setPlayerStart(fx.api.hexIndex(50, 50), 0, 0);
        CHECK(fx.api.mutated());
    }
    SECTION("newMap counts as a mutation") {
        ScriptApiFixture fx{ false };
        fx.api.newMap();
        CHECK(fx.api.mutated());
    }
    SECTION("placing an object counts as a mutation") {
        ScriptApiFixture fx{ false };
        REQUIRE(fx.api.placeObject(0x02000066u, 0x02000000u, 20100, 0));
        CHECK(fx.api.mutated());
    }
}

TEST_CASE("MapScriptApi setPlayerStart writes the map header", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    const int hex = fx.api.hexIndex(99, 99);
    fx.api.setPlayerStart(hex, 2, 0);
    const auto& header = fx.mapFile().header;
    CHECK(header.player_default_position == static_cast<uint32_t>(hex));
    CHECK(header.player_default_orientation == 2u);
    CHECK(header.player_default_elevation == 0u);

    // Out-of-range values raise rather than write a bogus header.
    CHECK_THROWS(fx.api.setPlayerStart(-1, 0, 0));                     // off-grid hex
    CHECK_THROWS(fx.api.setPlayerStart(hex, 6, 0));                    // orientation > 5
    CHECK_THROWS(fx.api.setPlayerStart(hex, 0, Map::ELEVATION_COUNT)); // elevation out of range
}

TEST_CASE("Placed objects store a zero pixel offset, not the hex's screen position", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };

    // A MapObject's x/y are a pixel offset FROM its hex, not a position: the engine renders at
    // tileToScreenXY(tile) + art offset + (x, y), and sx/sy are recomputed every frame. Writing
    // the hex's own screen coordinates here (thousands of pixels for any hex past the top-left
    // corner) blits the object outside the view, so it loads but is never visible in game.
    // Pick a hex far from the origin - at hex 0 the bug would store 0 and hide itself.
    constexpr int FAR_HEX = 20100;
    REQUIRE(fx.api.placeObject(0x02000066u, 0x02000000u, FAR_HEX, 0));
    REQUIRE(fx.api.placeExitGrid(20200, 5, 12345, 1, 3));

    const auto& objs = fx.mapFile().map_objects[ELEV];
    REQUIRE(objs.size() == 2);
    for (const auto& object : objs) {
        CHECK(object->x == 0u);
        CHECK(object->y == 0u);
    }

    // The hex those objects sit on does have a large screen position - the value that used to leak
    // into x/y. Without this the test would pass on a hexgrid that reported 0,0 for every hex.
    const auto hex = fx.hexgrid.getHexByPosition(FAR_HEX);
    REQUIRE(hex.has_value());
    CHECK(hex->get().x() > 0);
    CHECK(hex->get().y() > 0);
}

TEST_CASE("MapScriptApi placeExitGrid records a MISC exit-grid object", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };
    // data-only: exit-grid art isn't mounted, but the .map fields are what matters here.

    constexpr int HEX = 20100;
    // A worldmap exit (destMapId -2): the engine ignores destHex for it.
    REQUIRE(fx.api.placeExitGrid(HEX, -2, 0, 0, 0));
    CHECK(fx.api.placedObjects() == 1);

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
    REQUIRE(fx.api.placeExitGrid(20200, 5, 12345, 1, 3));
    const auto& eg2 = *objs.back();
    CHECK(eg2.pro_pid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(eg2.frm_pid == ExitGrid::greenFrm(ExitGrid::DIR_BOTTOM));
    CHECK(eg2.exit_map == 5u);
    CHECK(eg2.exit_position == 12345u);
    CHECK(eg2.exit_elevation == 1u);
    CHECK(eg2.exit_orientation == 3u);

    // Invalid inputs raise (not a silently-dropped or bogus object).
    CHECK_THROWS(fx.api.placeExitGrid(-1, 0, 0, 0, 0));                     // off-grid placement hex
    CHECK_THROWS(fx.api.placeExitGrid(HEX, -3, 0, 0, 0));                   // bad destMapId
    CHECK_THROWS(fx.api.placeExitGrid(HEX, 0, -1, 0, 0));                   // destHex out of range
    CHECK_THROWS(fx.api.placeExitGrid(HEX, 0, 0, Map::ELEVATION_COUNT, 0)); // destElevation out of range
    CHECK_THROWS(fx.api.placeExitGrid(HEX, 0, 0, 0, 6));                    // orientation > 5
    CHECK(fx.api.placedObjects() == 2);                                     // the failed calls added nothing
}

TEST_CASE("MapScriptApi placeExitGridRect frames a rectangle of exit grids", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };
    // data-only: exit-grid art isn't mounted, but the placed markers' fields are what matters.

    const int center = fx.api.hexIndex(100, 100);
    const int placed = fx.api.placeExitGridRect(center, 600, 400, -2, 0, 0, 0);
    CHECK(placed > 0);
    CHECK(fx.api.placedObjects() == placed);

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
    CHECK_THROWS(fx.api.placeExitGridRect(-1, 600, 400, -2, 0, 0, 0));                        // off-grid centre
    CHECK_THROWS(fx.api.placeExitGridRect(center, 0, 400, -2, 0, 0, 0));                      // non-positive extent
    CHECK_THROWS(fx.api.placeExitGridRect(center, 600, 400, -2, 0, Map::ELEVATION_COUNT, 0)); // bad destElevation
}

TEST_CASE("MapScriptApi::proto builds a PID from a readable type name and id", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    // Pure (type << 24) | id; produces exactly the hex a script would otherwise hand-write.
    CHECK(fx.api.proto("scenery", 102) == 0x02000066u); // Scrub
    CHECK(fx.api.proto("item", 1) == 0x00000001u);
    CHECK(fx.api.proto("critter", 1) == 0x01000001u);
    CHECK(fx.api.proto("misc", 16) == 0x05000010u); // exit-grid range is MISC 16..23

    // Case-insensitive, singular or plural.
    CHECK(fx.api.proto("Scenery", 102) == fx.api.proto("scenery", 102));
    CHECK(fx.api.proto("items", 1) == fx.api.proto("item", 1));

    // Raises on an unknown type or an out-of-range id (not a silent bogus PID).
    CHECK_THROWS(fx.api.proto("vehicle", 1));
    CHECK_THROWS(fx.api.proto("scenery", 0));
    CHECK_THROWS(fx.api.proto("scenery", -1));
    CHECK_THROWS(fx.api.proto("scenery", 0x1000000));
}

TEST_CASE("MapScriptApi tile<->hex bridge is exact, covering, and invertible", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    SECTION("tileHexes is exactly hexTile's preimage over the whole grid") {
        // Build the preimage from hexTile, then compare every tile's tileHexes against it.
        // Counting mismatches manually keeps this a single assertion over 50k cells.
        std::vector<std::vector<int>> preimage(HexagonGrid::TILE_COUNT);
        int mapped = 0;
        for (int hex = 0; hex < HexagonGrid::POSITION_COUNT; ++hex) {
            const int tile = fx.api.hexTile(hex);
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
            if (fx.api.tileHexes(tile) != preimage[tile]) {
                ++mismatches;
            }
        }
        CHECK(mismatches == 0);
    }

    SECTION("an interior tile carries a small block of hexes, all mapping back") {
        const int tile = fx.api.tileIndex(50, 50);
        const auto hexes = fx.api.tileHexes(tile);
        REQUIRE(hexes.size() >= 3); // ~2x2 hexes per tile, shifted by the iso projection
        REQUIRE(hexes.size() <= 6);
        for (const int hex : hexes) {
            CHECK(fx.api.hexTile(hex) == tile);
        }
    }

    SECTION("off-grid input is the N/A value, not an error") {
        CHECK(fx.api.hexTile(-1) == -1);
        CHECK(fx.api.hexTile(HexagonGrid::POSITION_COUNT) == -1);
        CHECK(fx.api.tileHexes(-1).empty());
        CHECK(fx.api.tileHexes(HexagonGrid::TILE_COUNT).empty());
    }
}

TEST_CASE("MapScriptApi setElevation routes edits to the chosen elevation", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    fx.api.setElevation(2);
    REQUIRE(fx.api.paintFloor(10, SOME_TILE));
    CHECK(fx.api.getFloor(10) == SOME_TILE); // queries follow the switch

    fx.api.setElevation(0);
    CHECK(fx.api.getFloor(10) == EMPTY); // elevation 0 untouched
    CHECK(fx.map->getMapFile().tiles.at(2)[10].getFloor() == SOME_TILE);
    CHECK(fx.map->getMapFile().tiles.at(0)[10].getFloor() == EMPTY);

    CHECK_THROWS(fx.api.setElevation(-1));
    CHECK_THROWS(fx.api.setElevation(Map::ELEVATION_COUNT));
}

TEST_CASE("MapScriptApi hexesOnScreenRect matches the exit-grid rectangle walk", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };

    const int center = fx.api.hexIndex(100, 100);
    const auto border = fx.api.hexesOnScreenRect(center, 96, 48);
    REQUIRE_FALSE(border.empty());
    CHECK(std::ranges::is_sorted(border));
    CHECK(std::ranges::adjacent_find(border) == border.end()); // deduplicated

    // The same hexes placeExitGridRect walks: place a rect and compare the marker positions.
    const int placed = fx.api.placeExitGridRect(center, 96, 48, -2, 0, 0, 0);
    CHECK(placed == static_cast<int>(border.size()));
    std::set<int> markerHexes;
    for (const auto& object : fx.map->getMapFile().map_objects.at(ELEV)) {
        markerHexes.insert(object->position);
    }
    CHECK(markerHexes == std::set<int>(border.begin(), border.end()));

    CHECK_THROWS(fx.api.hexesOnScreenRect(-1, 96, 48));
    CHECK_THROWS(fx.api.hexesOnScreenRect(center, 0, 48));
}

TEST_CASE("MapScriptApi reads a reference map's full floor and typed objects", "[map_script_api][scripting]") {
    // Author the reference with the api itself — floor paints plus exit grids (MISC markers need
    // no proto to write or parse) — then mount it at the production layout (maps/<name>.map) and
    // read it back through the reference queries. Self-contained: no proto stubs, no shipped data.
    const test::TempDir rootDir{ "geck_scriptapi_refmap" };
    const fs::path& root = rootDir.path();
    {
        ScriptApiFixture author{ false };
        REQUIRE(author.api.paintFloor(0, SOME_TILE));
        REQUIRE(author.api.paintFloor(9999, SOME_TILE));
        REQUIRE(author.api.placeExitGrid(20100, -2, 0, 0, 0));
        REQUIRE(author.api.placeExitGrid(20102, -2, 0, 0, 0));
        test::writeMapFile(author.mapFile(), root / "maps/ref.map");
    }

    ScriptApiFixture fx;
    fx.resources.files().addDataPath(root);

    const auto floor = fx.api.mapFloorAt("maps/ref.map", 0);
    REQUIRE(floor.size() == Map::TILES_PER_ELEVATION);
    CHECK(floor[0] == SOME_TILE);
    CHECK(floor[9999] == SOME_TILE);
    CHECK(floor[5000] == Map::EMPTY_TILE);
    CHECK_THROWS(fx.api.mapFloorAt("maps/ref.map", 3)); // out-of-range elevation raises

    // Objects come back through their type filter as (pid, hex, direction) triples.
    const auto misc = fx.api.mapObjectsAt("maps/ref.map", 0, "misc");
    REQUIRE(misc.size() == 6); // two exit-grid markers
    const std::set<int> hexes{ misc[1], misc[4] };
    CHECK(hexes == std::set<int>{ 20100, 20102 });
    for (const std::size_t at : { std::size_t{ 0 }, std::size_t{ 3 } }) {
        CHECK(ProtoId(static_cast<uint32_t>(misc[at])).objectType() == ObjectType::Misc);
        CHECK(misc[at + 2] == 0);
    }
    CHECK(fx.api.mapObjectsAt("maps/ref.map", 0, "wall").empty()); // no walls on this map

    CHECK_THROWS(fx.api.mapObjectsAt("maps/ref.map", 0, "gizmo")); // unknown type name
    CHECK_THROWS(fx.api.mapObjectsAt("no/such/map.map", 0, "wall"));
}

TEST_CASE("MapScriptApi quilts a floor learned from a reference map", "[map_script_api][scripting]") {
    const test::TempDir rootDir{ "geck_scriptapi_quilt" };
    const fs::path& root = rootDir.path();
    geck::test::writeCheckerboardReference(root);

    ScriptApiFixture fx;
    fx.resources.files().addDataPath(root);
    fx.api.setSeed(99);

    SECTION("quiltFloorRect paints the whole rectangle, reproducing the reference's blending") {
        const int painted = fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 9, 9);
        CHECK(painted == 100);
        CHECK(fx.api.paintedTiles() == 100);
        for (int row = 0; row < 10; ++row) {
            for (int col = 0; col < 10; ++col) {
                const uint16_t id = fx.api.getFloorXY(col, row);
                CHECK((id == 271 || id == 272)); // only the reference's vocabulary
            }
        }
        // The checkerboard's alternation is the only adjacency the reference ever shows, so the
        // synthesized floor must alternate everywhere too — the seams look authored.
        for (int row = 0; row < 10; ++row) {
            for (int col = 1; col < 10; ++col) {
                CHECK(fx.api.getFloorXY(col, row) != fx.api.getFloorXY(col - 1, row));
            }
        }
        // Quilting is undoable like every mutator (the fixture run isn't batched, but each paint
        // went through the controller).
        CHECK(fx.undoStack.canUndo());

        // A clean run's fidelity stats: everything painted, nothing repaired, no seam the
        // reference didn't show. (Before any quilt the stats are all zeros.)
        const auto stats = fx.api.quiltStats();
        REQUIRE(stats.size() == 6);
        CHECK(stats[0] == 100); // painted
        CHECK(stats[1] > 0);    // blocks placed
        CHECK(stats[4] == 0);   // repaired cells
        CHECK(stats[5] == 0);   // unresolved seams
    }

    SECTION("the same seed reproduces the same floor") {
        const int painted = fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 7, 7);
        CHECK(painted == 64);

        ScriptApiFixture again;
        again.resources.files().addDataPath(root);
        again.api.setSeed(99);
        CHECK(again.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 7, 7) == 64);
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                CHECK(fx.api.getFloorXY(col, row) == again.api.getFloorXY(col, row));
            }
        }
    }

    SECTION("quilting blends into pre-existing tiles at the border") {
        // Lay the checkerboard directly except a 2x2 hole, then quilt the hole: the only
        // seam-valid continuation is the surrounding pattern's own phase.
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 6; ++col) {
                if (row < 2 || row > 3 || col < 2 || col > 3) {
                    REQUIRE(fx.api.paintFloorXY(col, row, static_cast<uint16_t>(271 + (col + row) % 2)));
                }
            }
        }
        const int painted = fx.api.quiltFloorTiles("maps/ref.map", 0,
            { fx.api.tileIndex(2, 2), fx.api.tileIndex(3, 2), fx.api.tileIndex(2, 3), fx.api.tileIndex(3, 3) });
        CHECK(painted == 4);
        for (int row = 2; row <= 3; ++row) {
            for (int col = 2; col <= 3; ++col) {
                CHECK(fx.api.getFloorXY(col, row) == 271 + (col + row) % 2);
            }
        }
    }

    SECTION("a plan sink captures the quilt without committing") {
        pattern::FillPlan plan;
        fx.api.setPlanSink(&plan);
        const int painted = fx.api.quiltFloorTiles("maps/ref.map", 0, fx.api.tilesInRect(0, 0, 4, 4));
        CHECK(painted == 25);
        CHECK(plan.tiles.size() == 25);
        CHECK(fx.api.getFloor(0) == EMPTY); // nothing committed to the map
        CHECK_FALSE(fx.undoStack.canUndo());
    }

    SECTION("duplicate and off-grid indices are skipped, not errors") {
        const int painted = fx.api.quiltFloorTiles("maps/ref.map", 0,
            { 5, 5, -3, HexagonGrid::TILE_COUNT, 6 });
        CHECK(painted == 2);
    }

    SECTION("genuine failures raise") {
        CHECK_THROWS(fx.api.quiltFloorRect("no/such/map.map", 0, 0, 0, 3, 3));
        CHECK_THROWS(fx.api.quiltFloorRect("maps/ref.map", 3, 0, 0, 3, 3)); // out-of-range elevation
        // An elevation the reference lacks — or carries with an all-empty floor — teaches nothing.
        CHECK_THROWS(fx.api.quiltFloorRect("maps/ref.map", 1, 0, 0, 3, 3));
    }
}

TEST_CASE("MapScriptApi quiltExclude masks filler ids out of the reference", "[map_script_api][scripting]") {
    const test::TempDir rootDir{ "geck_scriptapi_quilt_excl" };
    const fs::path& root = rootDir.path();
    geck::test::writeCheckerboardReference(root);

    ScriptApiFixture fx;
    fx.resources.files().addDataPath(root);
    fx.api.setSeed(99);

    SECTION("an excluded id is never learned or emitted") {
        // Excluding 272 leaves only 271 cells in the reference, all isolated (a checkerboard
        // minus one colour has no adjacent same-vocabulary pairs) — the quilt must still cover
        // the region, using only the remaining id.
        fx.api.quiltExclude({ 272 });
        const int painted = fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 5, 5);
        CHECK(painted == 36);
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 6; ++col) {
                CHECK(fx.api.getFloorXY(col, row) == 271); // 272 never emitted
            }
        }
    }

    SECTION("clearing the exclusion restores the full vocabulary") {
        fx.api.quiltExclude({ 272 });
        fx.api.quiltExclude({});
        fx.api.quiltFloorRect("maps/ref.map", 0, 20, 20, 27, 27);
        bool saw272 = false;
        for (int row = 20; row <= 27 && !saw272; ++row) {
            for (int col = 20; col <= 27 && !saw272; ++col) {
                saw272 = fx.api.getFloorXY(col, row) == 272;
            }
        }
        CHECK(saw272);
    }

    SECTION("excluding the whole vocabulary raises instead of quilting nothing") {
        fx.api.quiltExclude({ 271, 272 });
        CHECK_THROWS(fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 3, 3));
    }
}

TEST_CASE("MapScriptApi quiltObjects transplants the reference objects on copied cells", "[map_script_api][scripting]") {
    // Author the checkerboard reference PLUS misc exit-grid markers standing on it (MISC needs no
    // proto to author or transplant headlessly), then quilt and pull the markers across.
    const test::TempDir rootDir{ "geck_scriptapi_quilt_obj" };
    const fs::path& root = rootDir.path();
    uint32_t markerPid = 0;
    {
        ScriptApiFixture author{ false };
        for (int row = 0; row < 12; ++row) {
            for (int col = 0; col < 12; ++col) {
                REQUIRE(author.api.paintFloorXY(col, row, static_cast<uint16_t>(271 + (col + row) % 2)));
            }
        }
        // Markers on hexes over the board's interior; every reference cell is identical up to the
        // checkerboard phase, so quilting reuses these cells and must carry the markers along.
        REQUIRE(author.api.placeExitGrid(author.api.hexIndex(8, 8), -2, 0, 0, 0));
        REQUIRE(author.api.placeExitGrid(author.api.hexIndex(12, 10), -2, 0, 0, 0));
        markerPid = author.mapFile().map_objects.at(ELEV).front()->pro_pid;
        test::writeMapFile(author.mapFile(), root / "maps/ref.map");
    }

    ScriptApiFixture fx{ false };
    fx.resources.files().addDataPath(root);
    fx.api.setSeed(99);

    SECTION("markers ride along with the cells they stand on") {
        REQUIRE(fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 19, 19) == 400);
        const int placed = fx.api.quiltObjects("misc", {});
        CHECK(placed > 0);
        // Every transplanted marker stands on a tile inside the quilted rectangle.
        for (const auto& object : fx.mapFile().map_objects.at(ELEV)) {
            const int tile = fx.api.hexTile(object->position);
            CHECK(tile >= 0);
            CHECK(fx.api.tileCol(tile) <= 19);
            CHECK(fx.api.tileRow(tile) <= 19);
            CHECK(object->pro_pid == markerPid);
        }
        CHECK(static_cast<int>(fx.mapFile().map_objects.at(ELEV).size()) == placed);
    }

    SECTION("excludePids drops a proto from the transplant") {
        REQUIRE(fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 19, 19) == 400);
        CHECK(fx.api.quiltObjects("misc", { static_cast<int>(markerPid) }) == 0);
    }

    SECTION("a type the reference does not use transplants nothing") {
        REQUIRE(fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 9, 9) == 100);
        CHECK(fx.api.quiltObjects("scenery", {}) == 0);
    }

    SECTION("genuine failures raise") {
        CHECK_THROWS(fx.api.quiltObjects("misc", {})); // no quilt has run yet
        fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 3, 3);
        CHECK_THROWS(fx.api.quiltObjects("gizmo", {})); // unknown type name
    }
}

TEST_CASE("MapScriptApi copyObjects copies a reference elevation's objects onto the same hexes", "[map_script_api][scripting]") {
    // A reference with misc exit-grid markers (MISC needs no proto to author or copy headlessly).
    const test::TempDir rootDir{ "geck_scriptapi_copy_obj" };
    const fs::path& root = rootDir.path();
    uint32_t markerPid = 0;
    std::vector<int> markerHexes;
    {
        ScriptApiFixture author{ false };
        markerHexes = { author.api.hexIndex(8, 8), author.api.hexIndex(12, 10), author.api.hexIndex(31, 57) };
        for (const int hex : markerHexes) {
            REQUIRE(author.api.placeExitGrid(hex, -2, 0, 0, 0));
        }
        markerPid = author.mapFile().map_objects.at(ELEV).front()->pro_pid;
        test::writeMapFile(author.mapFile(), root / "maps/ref.map");
    }

    ScriptApiFixture fx{ false };
    fx.resources.files().addDataPath(root);

    SECTION("every object lands on its own hex, proto and art verbatim") {
        CHECK(fx.api.copyObjects("maps/ref.map", ELEV, { "misc" }, {}) == 3);
        std::vector<int> copied;
        for (const auto& object : fx.mapFile().map_objects.at(ELEV)) {
            CHECK(object->pro_pid == markerPid);
            copied.push_back(object->position);
        }
        std::ranges::sort(copied);
        std::ranges::sort(markerHexes);
        CHECK(copied == markerHexes);
    }

    SECTION("excludePids, another type or an empty elevation copy nothing") {
        CHECK(fx.api.copyObjects("maps/ref.map", ELEV, { "misc" }, { static_cast<int>(markerPid) }) == 0);
        CHECK(fx.api.copyObjects("maps/ref.map", ELEV, { "wall", "scenery" }, {}) == 0);
        CHECK(fx.api.copyObjects("maps/ref.map", ELEV + 1, { "misc" }, {}) == 0);
    }

    SECTION("copyExitGrids retargets the markers on the listed hexes, keeping their direction") {
        // The authored markers lead to the world map (brown art); retarget two of them to map 5.
        const std::vector<int> picked = { markerHexes[0], markerHexes[2] };
        CHECK(fx.api.copyExitGrids("maps/ref.map", ELEV, picked, 5, 1234, 0, 0) == 2);
        const auto& objects = fx.mapFile().map_objects.at(ELEV);
        REQUIRE(objects.size() == 2);
        const int dir = static_cast<int>(markerPid - ExitGrid::FIRST_EXIT_GRID_PID);
        for (const auto& object : objects) {
            CHECK(std::ranges::find(picked, object->position) != picked.end());
            CHECK(object->pro_pid == markerPid);
            CHECK(object->frm_pid == ExitGrid::greenFrm(dir));
            CHECK(object->exit_map == 5);
            CHECK(object->exit_position == 1234);
        }
        CHECK_THROWS(fx.api.copyExitGrids("maps/ref.map", ELEV, picked, 5, 40000, 0, 0)); // off-grid destination
    }

    SECTION("genuine failures raise") {
        CHECK_THROWS(fx.api.copyObjects("maps/ref.map", ELEV, { "misc", "gizmo" }, {})); // unknown type name
        CHECK_THROWS(fx.api.copyObjects("maps/nope.map", ELEV, { "misc" }, {}));         // unreadable map
    }
}

TEST_CASE("MapScriptApi quiltSource learns only from the reference sub-rectangle", "[map_script_api][scripting]") {
    // Two vocabularies side by side: left half checkerboard 271/272, right half 273/274.
    const test::TempDir rootDir{ "geck_scriptapi_quilt_src" };
    const fs::path& root = rootDir.path();
    {
        ScriptApiFixture author{ false };
        for (int row = 0; row < 12; ++row) {
            for (int col = 0; col < 24; ++col) {
                const uint16_t base = col < 12 ? 271 : 273;
                REQUIRE(author.api.paintFloorXY(col, row, static_cast<uint16_t>(base + (col + row) % 2)));
            }
        }
        test::writeMapFile(author.mapFile(), root / "maps/ref.map");
    }

    ScriptApiFixture fx{ false };
    fx.resources.files().addDataPath(root);
    fx.api.setSeed(99);

    fx.api.quiltSource(12, 0, 23, 11); // learn from the RIGHT half only
    REQUIRE(fx.api.quiltFloorRect("maps/ref.map", 0, 0, 0, 9, 9) == 100);
    for (int row = 0; row < 10; ++row) {
        for (int col = 0; col < 10; ++col) {
            const uint16_t id = fx.api.getFloorXY(col, row);
            CHECK((id == 273 || id == 274)); // never the left half's vocabulary
        }
    }

    fx.api.quiltSource(-1, -1, -1, -1); // cleared: the whole reference is learnable again
    REQUIRE(fx.api.quiltFloorRect("maps/ref.map", 0, 20, 20, 29, 29) == 100);
}

TEST_CASE("MapScriptApi tilesByPrefix resolves a tile family from tiles.lst", "[map_script_api][scripting]") {
    // A data tree holding just a tiles.lst, so tileId/tilesByPrefix have real entries to resolve.
    const test::TempDir data{ "geck_scriptapi_tiles" };
    test::writeFile(data.path() / "art/tiles/tiles.lst", "edg5000.frm\nedg5001.frm\ncav1000.frm\n");
    ScriptApiFixture fx;
    fx.resources.files().addDataPath(data.path());

    const auto edges = fx.api.tilesByPrefix("EDG"); // case-insensitive, like tileId
    REQUIRE(edges.size() == 2);
    CHECK(edges.at("edg5000") == 0);
    CHECK(edges.at("edg5001") == 1);
    CHECK(edges.at("edg5001") == fx.api.tileId("edg5001")); // ids agree with tileId

    CHECK(fx.api.tilesByPrefix("cav").at("cav1000") == 2);
    CHECK(fx.api.tilesByPrefix("zzz").empty()); // unmatched prefix is a valid empty result
}

TEST_CASE("MapScriptApi reports genuine failures by throwing, not silently", "[map_script_api][scripting]") {
    ScriptApiFixture fx;

    SECTION("data-dependent queries throw when no data is mounted (no silent empty)") {
        // These can't produce a real answer without game data, so they raise rather than hand back
        // an empty result indistinguishable from a valid "nothing here".
        CHECK_THROWS(fx.api.tileId("edg5000"));
        CHECK_THROWS(fx.api.tilesByPrefix("edg"));
        CHECK_THROWS(fx.api.mapFloorAt("maps/desert1.map", 0));
        CHECK_THROWS(fx.api.mapObjectsAt("maps/desert1.map", 0, "wall"));
        CHECK_THROWS(fx.api.protoBlocks(0x02000066));
        CHECK_THROWS(fx.api.protoFlat(0x02000066));
        CHECK_THROWS(fx.api.protoSceneryType(0x02000066));
        CHECK_THROWS(fx.api.protoFid(0x02000066));
        CHECK_THROWS(fx.api.protoArtFrames(0x02000066));
        CHECK_THROWS(fx.api.artRect(0x02000066, 20100));
        CHECK_THROWS(fx.api.mapScenery("maps/desert1.map"));
        CHECK_THROWS(fx.api.mapScenery("no/such/map.map"));
        CHECK_THROWS(fx.api.mapSceneryHistogram("maps/desert1.map"));
        CHECK_THROWS(fx.api.mapFloorTiles("maps/desert1.map"));
        CHECK_THROWS(fx.api.quiltFloorRect("maps/desert1.map", 0, 0, 0, 3, 3));
        CHECK_THROWS(fx.api.protoName(0x02000066));
    }

    SECTION("listMaps is a graceful enumeration -> empty (no maps is a valid answer)") {
        CHECK(fx.api.listMaps().empty());
    }

    SECTION("placeProto stays a status return (skip), not a throw") {
        CHECK_FALSE(fx.api.placeProto(0x02000066u, -1, 0));    // off-grid hex
        CHECK_FALSE(fx.api.placeProto(0x02000066u, 20100, 0)); // valid hex, but the proto art can't load
        CHECK(fx.api.placedObjects() == 0);
    }
}

TEST_CASE("MapScriptApi noise2d is pure: in [0,1] and deterministic", "[map_script_api][scripting]") {
    ScriptApiFixture fx;
    for (int xi = 0; xi < 4; ++xi) {
        for (int yi = 0; yi < 4; ++yi) {
            const double x = xi * 1.3;
            const double y = yi * 1.7;
            const double n = fx.api.noise2d(x, y);
            CHECK(n >= 0.0);
            CHECK(n <= 1.0);
            CHECK(fx.api.noise2d(x, y) == n); // same input -> same output
        }
    }
    CHECK(fx.api.noise2d(1.5, 2.5) != fx.api.noise2d(40.5, 80.5)); // varies across the field
}

TEST_CASE("The plan sink caps a run at a multiple of the bound area's footprint", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };
    // Headless (buildSprites=false): the sink records objects with a null visual, no art needed.

    // Footprint = 2 hexes + 1 floor tile = 3 -> cap = 8 * 3 = 24 entries per list.
    EditArea area;
    area.hexes = { 20100, 20101 };
    area.floorTiles = { 0 };
    pattern::FillPlan plan;
    fx.api.setArea(&area);
    fx.api.setPlanSink(&plan);

    constexpr uint32_t PRO = 0x02000066;
    constexpr uint32_t FRM = 0x02000000;

    // Objects: the 25th and later placements are refused, reported like off-grid ones.
    int placed = 0;
    for (int i = 0; i < 40; ++i) {
        if (fx.api.placeObject(PRO, FRM, 20100, 0)) {
            ++placed;
        }
    }
    CHECK(placed == 24);
    CHECK(plan.objects.size() == 24);
    CHECK(plan.dropped == 16);
    CHECK(fx.api.placedObjects() == 24); // refused placements are not counted as placed

    // Tiles have the same independent cap: repaints are entries too, so a runaway loop
    // repainting one tile stops growing the plan at the cap.
    int painted = 0;
    for (int i = 0; i < 40; ++i) {
        if (fx.api.paintFloor(0, SOME_TILE)) {
            ++painted;
        }
    }
    CHECK(painted == 24);
    CHECK(plan.tiles.size() == 24);
    CHECK(plan.dropped == 16 + 16);
}

TEST_CASE("The plan sink cap trims a stamp's bulk append", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };

    // Footprint = 1 hex -> cap = 8 entries per list.
    EditArea area;
    area.hexes = { 20100 };
    pattern::FillPlan plan;
    fx.api.setArea(&area);
    fx.api.setPlanSink(&plan);

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
    fx.api.addStamp("big", big);

    const int placed = fx.api.placeStamp("big", 20100, 0);
    CHECK(placed == 8);
    CHECK(plan.objects.size() == 8);
    CHECK(plan.dropped == 4);
}

TEST_CASE("A sink with no bound area stays uncapped", "[map_script_api][scripting]") {
    ScriptApiFixture fx{ false };

    // Programmatic sink use (no fill area): the cap does not apply.
    pattern::FillPlan plan;
    fx.api.setPlanSink(&plan);

    for (int i = 0; i < 50; ++i) {
        REQUIRE(fx.api.placeObject(0x02000066u, 0x02000000u, 20100 + i, 0));
    }
    CHECK(plan.objects.size() == 50);
    CHECK(plan.dropped == 0);
}

// artRect places a proto's art the way the engine draws it (fallout2-ce object.cc objectGetRect over
// tile.cc tileToScreenXY): the frame hangs from the hex's point + (16, 8) + the FRM's direction shift,
// centred on x with its bottom on y. Cave rims are laid by abutting these rects pixel-exactly, so the
// geometry is pinned on three neighbouring hexes of one 16x120 strip with a (8, -4) shift.
TEST_CASE("MapScriptApi artRect gives the screen rectangle the engine draws a proto's art in", "[map_script_api][scripting]") {
    const test::TempDir rootDir{ "geck_artrect" };
    const fs::path& root = rootDir.path();
    test::writeFile(root / "proto" / "walls" / "walls.lst", "piece.pro\n");
    test::writeFile(root / "art" / "walls" / "walls.lst", "piece.frm\n");
    {
        Pro pro{ root / "proto" / "walls" / "piece.pro" };
        pro.header.PID = 0x03000001;
        pro.header.FID = 0x03000000; // walls.lst entry 0
        test::writePro(root / "proto" / "walls" / "piece.pro", pro);
    }
    {
        geck::test::ByteWriter frm;
        frm.be32(4).be16(1).be16(0).be16(1); // version, fps, action frame, frames per direction
        for (int i = 0; i < 6; ++i) {
            frm.be16(8); // shiftX
        }
        for (int i = 0; i < 6; ++i) {
            frm.be16(static_cast<uint16_t>(-4)); // shiftY
        }
        for (int i = 0; i < 6; ++i) {
            frm.be32(0); // one shared direction
        }
        frm.be32(12 + 16 * 120);
        frm.be16(16).be16(120).be32(16 * 120).be16(0).be16(0).fill(1, 16 * 120);
        test::writeBytes(root / "art" / "walls" / "piece.frm", frm.data());
    }

    ScriptApiFixture fx;
    fx.resources.files().addDataPath(root.string());

    // Row 10. Hex 2199 is the rightmost column (counted from the right: 0, even): its point is
    // (16 * row, 12 * row) = (160, 120), so the anchor is (160 + 16 + 8, 120 + 8 - 4) = (184, 124).
    CHECK(fx.api.artRect(0x03000001, 2199) == std::vector<int>{ 176, 5, 191, 124 });
    // One column left (odd): 32px further right, same height.
    CHECK(fx.api.artRect(0x03000001, 2198) == std::vector<int>{ 208, 5, 223, 124 });
    // Two columns left (even again): 48px right and 12px up — and it starts on the pixel after 2198 ends.
    CHECK(fx.api.artRect(0x03000001, 2197) == std::vector<int>{ 224, -7, 239, 112 });
    CHECK_THROWS(fx.api.artRect(0x03000001, -1));
}
