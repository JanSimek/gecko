#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "editor/HexGeometry.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "pattern/Pattern.h"
#include "pattern/PatternStamper.h"

#include "support/ControllerFixture.h"

using namespace geck;
using namespace geck::pattern;
using geck::test::ControllerFixture;

namespace {

constexpr int ANCHOR = 100 * hexgrid::WIDTH + 100; // col 100, row 100
constexpr int TARGET = 50 * hexgrid::WIDTH + 50;   // col 50, row 50

PatternVariant sampleVariant() {
    PatternVariant v;
    v.label = "entrance west";
    v.anchorHex = ANCHOR;
    v.objects.push_back(PatternObject{ 0, 0, 33555201u, 16777345u, 0u, 0u });
    v.objects.push_back(PatternObject{ 1, 0, 33555202u, 16777346u, 2u, 0x10u });
    v.objects.push_back(PatternObject{ 0, 1, 33555203u, 16777347u, 4u, 0u });
    v.floor.push_back(PatternTile{ 0, 0, 271 });
    return v;
}

} // namespace

TEST_CASE("PatternStamper::plan resolves object and tile positions", "[pattern][stamper]") {
    const PatternVariant v = sampleVariant();
    const auto plan = PatternStamper::plan(v, TARGET);

    REQUIRE(plan.objects.size() == 3);
    // The (0,0) object lands exactly on the target anchor, verbatim PID/direction.
    CHECK(plan.objects[0].hex == TARGET);
    CHECK(plan.objects[0].proPid == 33555201u);
    CHECK(plan.objects[0].frmPid == 16777345u);
    CHECK(plan.objects[0].direction == 0u);
    CHECK(plan.objects[1].direction == 2u);
    CHECK(plan.objects[1].flags == 0x10u);
    // Every object translates the same way translate() would.
    for (size_t i = 0; i < v.objects.size(); ++i) {
        const int authored = (hexgrid::rowOf(ANCHOR) + v.objects[i].dyHex) * hexgrid::WIDTH
            + (hexgrid::columnOf(ANCHOR) + v.objects[i].dxHex);
        CHECK(plan.objects[i].hex == hexgrid::translate(authored, ANCHOR, TARGET));
    }

    // floor tile re-anchors on the target hex's tile (col 25, row 25 -> index 2525).
    REQUIRE(plan.tiles.size() == 1);
    CHECK(plan.tiles[0].tileIndex == 25 * HexagonGrid::TILE_GRID_WIDTH + 25);
    CHECK_FALSE(plan.tiles[0].isRoof);
    CHECK(plan.tiles[0].tileId == 271);
}

TEST_CASE("PatternStamper::plan keeps tiles aligned with objects at any click parity", "[pattern][stamper]") {
    using namespace geck::hexgrid;
    PatternVariant aligned;
    aligned.anchorHex = 80 * WIDTH + 80; // col 80 (even), row 80
    // An object an ODD number of columns and rows from the anchor, plus the floor tile
    // that sits under it. Odd offsets are what expose the hex/2 rounding mismatch.
    aligned.objects.push_back(PatternObject{ 3, 3, 1u, 1u, 0u, 0u });
    aligned.floor.push_back(PatternTile{ (80 + 3) / 2 - 80 / 2, (80 + 3) / 2 - 80 / 2, 271 });

    // Stamp on an ODD-column, ODD-row target (opposite parity in both axes to the even
    // anchor) — the case that used to drift the tiles relative to the objects.
    for (const int target : { 50 * WIDTH + 51, 51 * WIDTH + 50, 51 * WIDTH + 51, 50 * WIDTH + 50 }) {
        const auto alignedPlan = PatternStamper::plan(aligned, target);
        REQUIRE(alignedPlan.objects.size() == 1);
        REQUIRE(alignedPlan.tiles.size() == 1);
        const int objHex = alignedPlan.objects[0].hex;
        const int objTile = (rowOf(objHex) / 2) * HexagonGrid::TILE_GRID_WIDTH + (columnOf(objHex) / 2);
        INFO("target " << target);
        CHECK(alignedPlan.tiles[0].tileIndex == objTile); // the floor tile still sits under the object
    }
}

TEST_CASE("PatternStamper::plan drops entries that fall off the grid", "[pattern][stamper]") {
    PatternVariant v;
    v.anchorHex = 0;                                              // top-left corner
    v.objects.push_back(PatternObject{ -5, -5, 1u, 1u, 0u, 0u }); // up-left of a corner anchor
    v.floor.push_back(PatternTile{ -5, -5, 100 });

    const auto plan = PatternStamper::plan(v, 0); // stamp back at the corner
    CHECK(plan.objects.empty());
    CHECK(plan.objectsDropped == 1);
    CHECK(plan.tiles.empty());
    CHECK(plan.tilesDropped == 1);
}

TEST_CASE("PatternStamper::stamp applies a variant as one undo entry", "[pattern][stamper]") {
    ControllerFixture fx;
    // Seed elevation 0 with a full set of empty tiles so the tile edits have somewhere
    // to land and a readable before-value.
    fx.mapFile().tiles[0] = std::vector<Tile>(
        Map::TILES_PER_ELEVATION, Tile(static_cast<uint16_t>(Map::EMPTY_TILE), static_cast<uint16_t>(Map::EMPTY_TILE)));

    PatternStamper stamper(fx.resources, fx.hexgrid, fx.controller, *fx.map);

    PatternVariant v = sampleVariant();
    v.roof.push_back(PatternTile{ 0, 0, 200 }); // floor + roof => two tile sections
    const auto plan = PatternStamper::plan(v, TARGET);
    const int floorTile = plan.tiles[0].tileIndex;
    const int roofTile = plan.tiles[1].tileIndex;

    const auto result = stamper.stamp(v, TARGET, 0);
    REQUIRE(result.success);
    // The fixture has no mounted game art, so object FRMs do not resolve and the objects
    // are skipped (their placement math is covered by PatternStamper::plan above). Tiles
    // need no art and are applied.
    CHECK(result.objectsPlaced == 0);
    CHECK(result.objectsFailed == 3);
    CHECK(result.tilesPainted == 2);

    CHECK(fx.mapFile().tiles[0][floorTile].getFloor() == 271);
    CHECK(fx.mapFile().tiles[0][roofTile].getRoof() == 200);

    // The whole stamp collapses to ONE undo entry, labelled with the variant — proof the
    // ScopedUndoBatch wrapped the edits rather than recording each separately.
    CHECK(fx.undoStack.lastUndoLabel() == "Stamp pattern: entrance west");

    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().tiles[0][floorTile].getFloor() == static_cast<uint16_t>(Map::EMPTY_TILE));
    CHECK(fx.mapFile().tiles[0][roofTile].getRoof() == static_cast<uint16_t>(Map::EMPTY_TILE));
    CHECK_FALSE(fx.undoStack.canUndo()); // single entry

    REQUIRE(fx.undoStack.redo());
    CHECK(fx.mapFile().tiles[0][floorTile].getFloor() == 271);
    CHECK(fx.mapFile().tiles[0][roofTile].getRoof() == 200);
}
