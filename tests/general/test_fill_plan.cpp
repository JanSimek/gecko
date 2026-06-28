#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "pattern/FillPlan.h"
#include "pattern/Pattern.h"
#include "pattern/PlacementBatch.h"
#include "scripting/EditArea.h"
#include "scripting/MapScriptApi.h"

#include "support/ControllerFixture.h"

// The A0 plan-sink core: MapScriptApi mutators record into a FillPlan instead of committing while a
// sink is installed (preview), and pattern::PlacementBatch::replay applies the captured plan as ONE
// undo entry (apply). The contract these tests pin down is what makes a fill preview byte-identical
// to the apply and collapses a whole fill into a single Ctrl-Z.

using namespace geck;
using geck::test::ControllerFixture;

namespace {
constexpr int ELEV = 0;
constexpr uint16_t TILE_A = 271;
constexpr uint16_t TILE_B = 300;
constexpr uint16_t EMPTY = static_cast<uint16_t>(Map::EMPTY_TILE);
constexpr uint32_t PRO = 0x02000066; // Scrub (scenery)
constexpr uint32_t FRM = 0x02000000; // arbitrary FID; not resolved in data-only mode

// Give elevation 0 a full set of empty tiles so paints have somewhere to land with a readable
// before-value. Objects are placed data-only (buildSprites=false) so they succeed without art.
void seedTiles(ControllerFixture& fx) {
    fx.mapFile().tiles[ELEV] = std::vector<Tile>(Map::TILES_PER_ELEVATION, Tile(EMPTY, EMPTY));
}
} // namespace

TEST_CASE("plan sink records edits without committing; replay applies them as one undo entry", "[scripting][fill]") {
    ControllerFixture fx;
    seedTiles(fx);
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, /*buildSprites*/ false);

    pattern::FillPlan plan;
    api.setPlanSink(&plan);

    REQUIRE(api.paintFloor(10, TILE_A));
    REQUIRE(api.paintFloor(11, TILE_B));
    REQUIRE(api.placeObject(PRO, FRM, 20100, 0));

    // Captured into the plan, fully built...
    CHECK(plan.tiles.size() == 2);
    REQUIRE(plan.objects.size() == 1);
    CHECK(plan.objects[0].mapObject->pro_pid == PRO);

    // ...but nothing committed: the map is untouched and there is no undo entry.
    CHECK(fx.mapFile().tiles[ELEV][10].getFloor() == EMPTY);
    CHECK(fx.mapFile().tiles[ELEV][11].getFloor() == EMPTY);
    CHECK(fx.mapFile().map_objects[ELEV].empty());
    CHECK_FALSE(fx.undoStack.canUndo());

    // Detach the sink and replay: now it commits, as a SINGLE undo entry.
    api.setPlanSink(nullptr);
    const auto r = pattern::PlacementBatch::replay(fx.controller, plan, /*buildSprites*/ false, "Fill selection");
    CHECK(r.tilesPainted == 2);
    CHECK(r.objectsPlaced == 1);

    CHECK(fx.mapFile().tiles[ELEV][10].getFloor() == TILE_A);
    CHECK(fx.mapFile().tiles[ELEV][11].getFloor() == TILE_B);
    REQUIRE(fx.mapFile().map_objects[ELEV].size() == 1);
    CHECK(fx.mapFile().map_objects[ELEV][0]->pro_pid == PRO);

    // One undo reverts the whole fill, and the stack is then empty (proof of one entry, not three).
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK(fx.mapFile().tiles[ELEV][10].getFloor() == EMPTY);
    CHECK(fx.mapFile().tiles[ELEV][11].getFloor() == EMPTY);
    CHECK(fx.mapFile().map_objects[ELEV].empty());
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("a captured-then-replayed run yields the same map as a direct run", "[scripting][fill]") {
    // Direct run: mutators commit immediately inside one batch.
    ControllerFixture direct;
    seedTiles(direct);
    {
        MapScriptApi api(direct.resources, direct.hexgrid, direct.controller, *direct.map, ELEV, false);
        api.beginBatch("Fill");
        REQUIRE(api.paintFloor(10, TILE_A));
        REQUIRE(api.placeObject(PRO, FRM, 20100, 0));
        REQUIRE(api.placeObject(PRO, FRM, 20101, 2));
        api.endBatch();
    }

    // Captured run: same calls into a sink, then replayed.
    ControllerFixture replayed;
    seedTiles(replayed);
    {
        MapScriptApi api(replayed.resources, replayed.hexgrid, replayed.controller, *replayed.map, ELEV, false);
        pattern::FillPlan plan;
        api.setPlanSink(&plan);
        REQUIRE(api.paintFloor(10, TILE_A));
        REQUIRE(api.placeObject(PRO, FRM, 20100, 0));
        REQUIRE(api.placeObject(PRO, FRM, 20101, 2));
        api.setPlanSink(nullptr);
        pattern::PlacementBatch::replay(replayed.controller, plan, false, "Fill");
    }

    CHECK(direct.mapFile().tiles[ELEV][10].getFloor() == replayed.mapFile().tiles[ELEV][10].getFloor());

    const auto& a = direct.mapFile().map_objects[ELEV];
    const auto& b = replayed.mapFile().map_objects[ELEV];
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i]->pro_pid == b[i]->pro_pid);
        CHECK(a[i]->frm_pid == b[i]->frm_pid);
        CHECK(a[i]->position == b[i]->position);
        CHECK(a[i]->direction == b[i]->direction);
    }
}

TEST_CASE("placeStamp under a plan sink is captured, not committed", "[scripting][fill][stamper]") {
    ControllerFixture fx;
    seedTiles(fx);
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    // A small prefab: two objects + one floor tile.
    pattern::Pattern pat;
    pat.name = "cluster";
    pattern::PatternVariant v;
    v.anchorHex = 100 * HexagonGrid::GRID_WIDTH + 100;
    v.objects.push_back({ 0, 0, PRO, FRM, 0u, 0u });
    v.objects.push_back({ 1, 0, PRO, FRM, 2u, 0u });
    v.floor.push_back({ 0, 0, TILE_A });
    pat.variants.push_back(v);
    api.addStamp("cluster", pat);

    pattern::FillPlan plan;
    api.setPlanSink(&plan);
    const int placed = api.placeStamp("cluster", 50 * HexagonGrid::GRID_WIDTH + 50, 0);

    // Without the sink-aware planInto path, placeStamp would open its own ScopedUndoBatch and mutate
    // the live map during preview. Here it must only record into the plan.
    CHECK(placed == 2);
    CHECK(plan.objects.size() == 2);
    CHECK(plan.tiles.size() == 1);
    CHECK(fx.mapFile().map_objects[ELEV].empty());
    CHECK_FALSE(fx.undoStack.canUndo());

    // Replay commits the whole stamp as one entry.
    api.setPlanSink(nullptr);
    const auto r = pattern::PlacementBatch::replay(fx.controller, plan, false, "Fill: stamp");
    CHECK(r.objectsPlaced == 2);
    CHECK(r.tilesPainted == 1);
    REQUIRE(fx.mapFile().map_objects[ELEV].size() == 2);
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK(fx.mapFile().map_objects[ELEV].empty());
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("the same seed yields an identical fill plan; a different seed differs", "[scripting][fill]") {
    // A seeded scatter over a strip of hexes, captured into a plan. Reproducibility is the contract
    // that lets a previewed fill match the apply and a saved recipe regenerate the same map.
    const auto buildPlan = [](uint32_t seed) {
        ControllerFixture fx;
        seedTiles(fx);
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
        api.setSeed(seed);
        pattern::FillPlan plan;
        api.setPlanSink(&plan);
        for (int t = 0; t < 300; ++t) {
            if (api.rng() < 0.4) {
                api.placeObject(PRO, FRM, 21000 + t, static_cast<uint32_t>(api.rngInt(0, 5)));
            }
        }
        return plan; // owns its MapObjects via shared_ptr, so it outlives the fixture
    };

    const pattern::FillPlan a = buildPlan(42);
    const pattern::FillPlan b = buildPlan(42);
    const pattern::FillPlan c = buildPlan(43);

    // Same seed -> identical placements (count, position and seeded direction).
    REQUIRE(a.objects.size() == b.objects.size());
    CHECK(a.objects.size() > 0); // the scatter actually placed something to compare
    bool identical = true;
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
        identical = identical
            && a.objects[i].mapObject->position == b.objects[i].mapObject->position
            && a.objects[i].mapObject->direction == b.objects[i].mapObject->direction
            && a.objects[i].mapObject->pro_pid == b.objects[i].mapObject->pro_pid;
    }
    CHECK(identical);

    // A different seed produces a different layout (a count or some position/direction differs).
    bool differs = a.objects.size() != c.objects.size();
    for (std::size_t i = 0; !differs && i < a.objects.size(); ++i) {
        differs = a.objects[i].mapObject->position != c.objects[i].mapObject->position
            || a.objects[i].mapObject->direction != c.objects[i].mapObject->direction;
    }
    CHECK(differs);
}

TEST_CASE("area accessors report the bound selection and test membership", "[scripting][fill]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);

    CHECK_FALSE(api.hasArea());
    CHECK(api.areaHexes().empty());
    CHECK_FALSE(api.areaContainsHex(100)); // no area bound -> nothing is inside

    EditArea area;
    area.hexes = { 100, 205, 410 }; // sorted ascending (the EditArea contract)
    area.floorTiles = { 50, 51, 52 };
    area.roofTiles = { 50 };
    api.setArea(&area);

    CHECK(api.hasArea());
    CHECK(api.areaHexes() == std::vector<int>{ 100, 205, 410 });
    CHECK(api.areaFloorTiles().size() == 3);
    CHECK(api.areaRoofTiles() == std::vector<int>{ 50 });

    CHECK(api.areaContainsHex(205));
    CHECK_FALSE(api.areaContainsHex(206));
    CHECK(api.areaContainsTile(52));
    CHECK_FALSE(api.areaContainsTile(53));

    api.setArea(nullptr); // clears
    CHECK_FALSE(api.hasArea());
    CHECK_FALSE(api.areaContainsHex(205));
}
