#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "selection/SelectionState.h"
#include "selection/SelectionDataProvider.h"
#include "selection/SelectionManager.h"
#include "util/Types.h"
#include "util/TileUtils.h"
#include "util/Constants.h"
#include "format/map/Map.h"
#include "editor/Object.h"
#include "editor/HexagonGrid.h"

#include "support/GraphicsTestEnv.h"

#include <memory>
#include <optional>
#include <utility>
#include <algorithm>
#include <vector>

using namespace geck;
using namespace geck::selection;

//==============================================================================
// Mock provider for SelectionManager tests.
//
// MockEditorWidget implements the narrow SelectionDataProvider interface so
// SelectionManager can be exercised without the real EditorWidget god-object
// (the point of the SelectionDataProvider seam). The hit-test methods use a deliberately simple
// fabricated coordinate formula; the real world->hex/tile conversion is covered
// by test_viewport_controller.cpp.
//==============================================================================

class MockEditorWidget : public geck::selection::SelectionDataProvider {
public:
    int currentElevation = 0;
    bool roofVisible = true;       // flip to simulate the roof layer being hidden
    bool objectsSelectable = true; // flip to simulate objects being on a hidden layer
    sf::Vector2u windowSize{ 800, 600 };

    // Backing data for the interface accessors.
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<sf::Sprite> floorSprites;
    std::vector<sf::Sprite> roofSprites;
    HexagonGrid hexGrid;
    Map::MapFile mapFile;

    MockEditorWidget() = default;

    // --- SelectionDataProvider: data accessors ---
    const std::vector<std::shared_ptr<Object>>& getObjects() const override { return objects; }
    const std::vector<sf::Sprite>& getFloorSprites() const override { return floorSprites; }
    const std::vector<sf::Sprite>& getRoofSprites() const override { return roofSprites; }
    const HexagonGrid* getHexagonGrid() const override { return &hexGrid; }
    Map::MapFile& getMapFile() override { return mapFile; }
    const Map::MapFile& getMapFile() const override { return mapFile; }
    int getCurrentElevation() const override { return currentElevation; }
    ViewportController* getViewportController() const override { return nullptr; }
    bool isRoofVisible() const override { return roofVisible; }
    bool isObjectSelectable(const std::shared_ptr<Object>&) const override { return objectsSelectable; }

    // --- SelectionDataProvider: hit tests ---
    std::optional<int> getTileAtPosition(sf::Vector2f worldPos, [[maybe_unused]] bool isRoof) override {
        // Deliberately simple, deterministic stub (not the real hit-test math, which
        // lives in ViewportController and is covered by test_viewport_controller.cpp):
        // x maps to a column, y to a row, in a row-major MAP_HEIGHT x MAP_WIDTH grid.
        if (worldPos.x < 0.0f || worldPos.y < 0.0f) {
            return std::nullopt;
        }

        const int col = static_cast<int>(worldPos.x) / 32;
        const int row = static_cast<int>(worldPos.y) / 24;

        if (col >= 0 && col < MAP_WIDTH && row >= 0 && row < MAP_HEIGHT) {
            return row * MAP_WIDTH + col;
        }
        return std::nullopt;
    }

    std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) override {
        return getTileAtPosition(worldPos, true);
    }

    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f /*worldPos*/) override {
        return {}; // Return empty for simplification
    }

    // --- Test setup helpers ---

    // Seed an elevation with a full TILES_PER_ELEVATION-sized tile vector.
    // Every floor is set; every roof defaults to EMPTY_TILE so callers can mark
    // specific roof tiles as present per elevation.
    void seedElevation(int elevation) {
        std::vector<Tile> tiles;
        tiles.reserve(TILES_PER_ELEVATION);
        for (int i = 0; i < TILES_PER_ELEVATION; ++i) {
            // floor = arbitrary non-empty texture id, roof = EMPTY_TILE.
            tiles.emplace_back(static_cast<uint16_t>(2), static_cast<uint16_t>(Map::EMPTY_TILE));
        }
        mapFile.tiles[elevation] = std::move(tiles);
    }

    // Mark a single roof tile as present (non-empty) on the given elevation.
    void setRoofPresent(int elevation, int tileIndex, uint16_t roofId = 7) {
        mapFile.tiles.at(elevation).at(tileIndex).setRoof(roofId);
    }
};

//==============================================================================
// SECTION: Mock provider hit-test sanity checks
//==============================================================================

TEST_CASE("Mock provider position-based tile hit tests", "[selection_manager][mock_provider]") {
    SECTION("Single click tile selection behavior") {
        MockEditorWidget mockEditor;

        // Test different world positions and verify correct tile selection.
        // Per the mock formula (col = x/32, row = y/24):
        sf::Vector2f testPos1(64.0f, 48.0f);  // -> col 2, row 2
        sf::Vector2f testPos2(128.0f, 96.0f); // -> col 4, row 4

        // Verify the mock returns reasonable tile indices
        auto tile1 = mockEditor.getTileAtPosition(testPos1, false);
        auto tile2 = mockEditor.getTileAtPosition(testPos2, false);

        REQUIRE(tile1.has_value());
        REQUIRE(tile2.has_value());
        REQUIRE(tile1.value() != tile2.value()); // Different positions should give different tiles

        // Verify tiles are within valid range
        REQUIRE(tile1.value() >= 0);
        REQUIRE(tile1.value() < TILES_PER_ELEVATION);
        REQUIRE(tile2.value() >= 0);
        REQUIRE(tile2.value() < TILES_PER_ELEVATION);
    }

    SECTION("Roof vs floor selection at same position") {
        MockEditorWidget mockEditor;
        sf::Vector2f testPos(100.0f, 75.0f);

        auto floorTile = mockEditor.getTileAtPosition(testPos, false);
        auto roofTile = mockEditor.getTileAtPosition(testPos, true);

        // Both should return the same tile index (same logical position)
        REQUIRE(floorTile.has_value());
        REQUIRE(roofTile.has_value());
        REQUIRE(floorTile.value() == roofTile.value());
    }

    SECTION("Invalid position handling") {
        MockEditorWidget mockEditor;

        // Test positions outside map bounds
        sf::Vector2f invalidPos1(-10.0f, -10.0f);     // Negative coordinates
        sf::Vector2f invalidPos2(10000.0f, 10000.0f); // Way outside map

        auto tile1 = mockEditor.getTileAtPosition(invalidPos1, false);
        auto tile2 = mockEditor.getTileAtPosition(invalidPos2, false);

        // Should return nullopt for invalid positions
        REQUIRE_FALSE(tile1.has_value());
        REQUIRE_FALSE(tile2.has_value());
    }
}

//==============================================================================
// SECTION: Real SelectionManager construction through the mock provider
//
// These tests close the testability gap: previously MockEditorWidget only
// proved the SelectionDataProvider interface compiled. Here we actually construct
// geck::selection::SelectionManager(mockWidget) and drive provider-backed paths
// end-to-end. We deliberately avoid the HEXES single-click branch, because the
// mock's getViewportController() returns nullptr and selectSingleAtPosition() in
// HEXES mode dereferences it (would segfault).
//==============================================================================

TEST_CASE("SelectionManager drives provider-backed selection paths", "[selection_manager_real]") {
    MockEditorWidget mockWidget;

    // Construct a REAL SelectionManager through the mock interface.
    geck::selection::SelectionManager mgr(mockWidget);

    SECTION("clearSelection on an empty manager keeps it empty") {
        REQUIRE_FALSE(mgr.hasSelection());
        mgr.clearSelection();
        REQUIRE_FALSE(mgr.hasSelection());
        REQUIRE(mgr.getCurrentSelection().count() == 0);
    }

    SECTION("selectAll(OBJECTS) reflects the provider's (empty) object list") {
        // Provider returns no objects; selectAll must read getObjects() and select none.
        mgr.selectAll(SelectionMode::OBJECTS, mockWidget.currentElevation);
        REQUIRE(mgr.getCurrentSelection().getObjects().size() == 0);
        REQUIRE_FALSE(mgr.hasSelection());
    }

    SECTION("selectAll(ROOF_TILES) reads per-elevation tile data from the provider") {
        // Seed elevation 0 with exactly three present roof tiles; all others EMPTY.
        mockWidget.seedElevation(0);
        mockWidget.setRoofPresent(0, 10);
        mockWidget.setRoofPresent(0, 250);
        mockWidget.setRoofPresent(0, 9999);

        mgr.selectAll(SelectionMode::ROOF_TILES, 0);

        // Only the three present roofs should be selected (provider-backed read of
        // getMapFile().tiles.at(elevation), filtering EMPTY_TILE).
        auto roofs = mgr.getCurrentSelection().getRoofTileIndices();
        REQUIRE(roofs.size() == 3);
        REQUIRE_THAT(roofs, Catch::Matchers::UnorderedEquals(std::vector<int>{ 10, 250, 9999 }));
    }

    SECTION("selectArea(FLOOR_TILES) succeeds via the provider's spatial index") {
        // The spatial index was never built, so it yields no tiles; the path must
        // still run cleanly through the provider and produce an (empty) selection.
        sf::FloatRect area({ 100.0f, 100.0f }, { 200.0f, 200.0f });
        auto result = mgr.selectArea(area, SelectionMode::FLOOR_TILES, mockWidget.currentElevation);
        REQUIRE(result.success);
        REQUIRE(mgr.getCurrentSelection().getFloorTileIndices().empty());
    }

    SECTION("selectArea(HEXES) is safe with an empty hex grid") {
        // getHexesInArea() iterates the provider's HexagonGrid; a default grid is empty.
        sf::FloatRect area({ 0.0f, 0.0f }, { 500.0f, 500.0f });
        auto result = mgr.selectArea(area, SelectionMode::HEXES, mockWidget.currentElevation);
        REQUIRE(result.success);
        REQUIRE(mgr.getCurrentSelection().getHexIndices().empty());
    }
}

//==============================================================================
// REGRESSION: moving a selected region must carry its roof tiles along (previously
// only objects moved; selected roofs were left behind). planRoofTileMove computes the
// block-safe before/after roof set the editor then applies through the tile-edit undo
// path; planRoofMoveForDrag derives the whole-tile delta from the drag positions.
//==============================================================================

namespace {
std::optional<TileChange> findTileChange(const std::vector<TileChange>& changes, int tileIndex) {
    for (const auto& change : changes) {
        if (change.tileIndex == tileIndex) {
            return change;
        }
    }
    return std::nullopt;
}
} // namespace

TEST_CASE("planSelectionTileMove moves selected floor and roof tiles as a block", "[selection_manager_real][roof_move]") {
    MockEditorWidget mockWidget;
    geck::selection::SelectionManager mgr(mockWidget);
    mockWidget.seedElevation(0);

    SECTION("a single roof tile vacates its source and fills the target") {
        mockWidget.setRoofPresent(0, 10, 7); // tile 10 = (row 0, col 10)
        mgr.selectAll(SelectionMode::ROOF_TILES, 0);

        const auto changes = mgr.planSelectionTileMove(1, 0); // down one row -> tile 110
        REQUIRE(changes.size() == 2);

        const auto source = findTileChange(changes, 10);
        const auto target = findTileChange(changes, 110);
        REQUIRE(source.has_value());
        REQUIRE(target.has_value());
        REQUIRE(source->isRoof);
        REQUIRE(source->before == 7);
        REQUIRE(source->after == Map::EMPTY_TILE);
        REQUIRE(target->before == Map::EMPTY_TILE);
        REQUIRE(target->after == 7);
    }

    SECTION("a floor tile moves with the same vacate-source/fill-target semantics") {
        // Seeded floors are all 2; give the source a distinct id so both ends change.
        mockWidget.mapFile.tiles.at(0).at(10).setFloor(5);
        mgr.getMutableSelection().items.emplace_back(SelectionType::FLOOR_TILE, 10);

        const auto changes = mgr.planSelectionTileMove(1, 0); // -> tile 110
        REQUIRE(changes.size() == 2);

        const auto source = findTileChange(changes, 10);
        const auto target = findTileChange(changes, 110);
        REQUIRE(source.has_value());
        REQUIRE(target.has_value());
        REQUIRE_FALSE(source->isRoof);
        REQUIRE(source->before == 5);
        REQUIRE(source->after == Map::EMPTY_TILE);
        REQUIRE(target->before == 2);
        REQUIRE(target->after == 5);
    }

    SECTION("a zero delta is a no-op") {
        mockWidget.setRoofPresent(0, 10, 7);
        mgr.selectAll(SelectionMode::ROOF_TILES, 0);
        REQUIRE(mgr.planSelectionTileMove(0, 0).empty());
    }

    SECTION("a move that would leave the map is rejected as a whole") {
        mockWidget.setRoofPresent(0, 9999, 7); // tile 9999 = (row 99, col 99)
        mgr.selectAll(SelectionMode::ROOF_TILES, 0);
        REQUIRE(mgr.planSelectionTileMove(1, 0).empty()); // row 100 is off the map
        REQUIRE(mgr.planSelectionTileMove(0, 1).empty()); // col 100 is off the map
    }

    SECTION("overlapping moves are block-safe: a source that is another's target keeps the moved roof") {
        mockWidget.setRoofPresent(0, 10, 7); // (row 0, col 10)
        mockWidget.setRoofPresent(0, 11, 8); // (row 0, col 11)
        mgr.selectAll(SelectionMode::ROOF_TILES, 0);

        const auto changes = mgr.planSelectionTileMove(0, 1); // shift right one column: 10->11, 11->12
        const auto t10 = findTileChange(changes, 10);
        const auto t11 = findTileChange(changes, 11);
        const auto t12 = findTileChange(changes, 12);
        REQUIRE(t10.has_value());
        REQUIRE(t11.has_value());
        REQUIRE(t12.has_value());
        REQUIRE(t10->after == Map::EMPTY_TILE);
        REQUIRE(t11->after == 7); // block-safe: not emptied by tile 10's vacate
        REQUIRE(t12->after == 8);
    }

    SECTION("with nothing selected there is nothing to move") {
        mgr.selectAll(SelectionMode::ROOF_TILES, 0); // none present -> empty selection
        REQUIRE(mgr.planSelectionTileMove(1, 1).empty());
    }
}

// planSelectionMoveForTranslation's delta comes from the real isometric tile projection
// (coordinatesToScreenPosition / nearest-centre hit-test), which the abstract mock deliberately
// does not replicate (see test_viewport_controller.cpp for that). So the positional mapping is
// covered there and by visual checks; here we only pin the projection-independent contract.
TEST_CASE("planSelectionMoveForTranslation with nothing selected is a no-op", "[selection_manager_real][roof_move]") {
    MockEditorWidget mockWidget;
    geck::selection::SelectionManager mgr(mockWidget);
    mockWidget.seedElevation(0);
    mgr.selectAll(SelectionMode::ROOF_TILES, 0); // none present -> empty selection
    REQUIRE(mgr.planSelectionMoveForTranslation(sf::Vector2f(32.0f, 0.0f)).empty());
}

//==============================================================================
// REGRESSION: clicking the same spot in ALL mode must cycle through the stack and
// finally deselect ("click again -> select the item underneath"). The input layer
// is what was broken (a click on a selected object did a no-op move instead of
// cycling), but the cycle-advance contract this depends on lives here, so guard it.
// No objects are placed (Object needs a graphics context), so the stack is the
// always-present roof -> floor pair the mock fabricates, then empty.
//==============================================================================
TEST_CASE("ALL-mode click cycles the stack then deselects", "[selection_manager_real][regression]") {
    MockEditorWidget mockWidget;
    geck::selection::SelectionManager mgr(mockWidget);

    // Mock maps worldPos (col*32, row*24) -> tile row*MAP_WIDTH + col.
    const sf::Vector2f clickPos{ 32.0f, 24.0f };
    const int tileIndex = 1 * MAP_WIDTH + 1;

    // 1st click: nothing selected -> top of the stack (roof).
    mgr.selectAtPosition(clickPos, SelectionMode::ALL, 0);
    REQUIRE(mgr.getCurrentSelection().count() == 1);
    REQUIRE(mgr.getCurrentSelection().getRoofTileIndices() == std::vector<int>{ tileIndex });

    // 2nd click: advances to the item underneath (floor).
    mgr.selectAtPosition(clickPos, SelectionMode::ALL, 0);
    REQUIRE(mgr.getCurrentSelection().count() == 1);
    REQUIRE(mgr.getCurrentSelection().getRoofTileIndices().empty());
    REQUIRE(mgr.getCurrentSelection().getFloorTileIndices() == std::vector<int>{ tileIndex });

    // 3rd click: nothing left underneath -> deselect.
    mgr.selectAtPosition(clickPos, SelectionMode::ALL, 0);
    REQUIRE_FALSE(mgr.hasSelection());
}

// Ctrl+drag is deselect-only: it removes already-selected covered items but never adds.
// Uses HEXES mode (the hex grid is pure geometry — no graphics context needed).
TEST_CASE("deselectArea removes only selected covered items (Ctrl+drag)", "[selection_manager_real][regression]") {
    MockEditorWidget mockWidget;
    geck::selection::SelectionManager mgr(mockWidget);

    // Build a drag rectangle around a known hex so the covered set is stable and non-empty.
    auto hex = mockWidget.hexGrid.getHexByPosition(20000);
    REQUIRE(hex.has_value());
    const sf::FloatRect area({ static_cast<float>(hex->get().x()) - 40.0f, static_cast<float>(hex->get().y()) - 40.0f },
        { 80.0f, 80.0f });

    SECTION("deselect over an empty selection adds nothing") {
        mgr.deselectArea(area, SelectionMode::HEXES, 0);
        CHECK(mgr.getCurrentSelection().getHexIndices().empty());
    }

    SECTION("deselect over an already-selected area removes those items") {
        mgr.selectArea(area, SelectionMode::HEXES, 0);
        REQUIRE_FALSE(mgr.getCurrentSelection().getHexIndices().empty());

        mgr.deselectArea(area, SelectionMode::HEXES, 0);
        CHECK(mgr.getCurrentSelection().getHexIndices().empty());
    }

    SECTION("itemsToDeselectInArea previews exactly what a deselect would remove") {
        // Drives the live Ctrl+drag preview: it must report the covered selected items
        // (and nothing when the area covers no selection) without mutating state.
        CHECK(mgr.itemsToDeselectInArea(area, SelectionMode::HEXES, 0).empty());

        mgr.selectArea(area, SelectionMode::HEXES, 0);
        const auto selectedCount = mgr.getCurrentSelection().getHexIndices().size();
        const auto toRemove = mgr.itemsToDeselectInArea(area, SelectionMode::HEXES, 0);
        CHECK(toRemove.size() == selectedCount);
        // Querying the preview must not change the selection.
        CHECK(mgr.getCurrentSelection().getHexIndices().size() == selectedCount);
    }
}

// Alt+drag adds the covered items to the selection without clearing what is already selected.
TEST_CASE("addArea accumulates into the selection (Alt+drag)", "[selection_manager_real][regression]") {
    MockEditorWidget mockWidget;
    geck::selection::SelectionManager mgr(mockWidget);

    auto hexA = mockWidget.hexGrid.getHexByPosition(20000);
    auto hexB = mockWidget.hexGrid.getHexByPosition(21000);
    REQUIRE(hexA.has_value());
    REQUIRE(hexB.has_value());
    const sf::FloatRect areaA({ static_cast<float>(hexA->get().x()) - 40.0f, static_cast<float>(hexA->get().y()) - 40.0f }, { 80.0f, 80.0f });
    const sf::FloatRect areaB({ static_cast<float>(hexB->get().x()) - 40.0f, static_cast<float>(hexB->get().y()) - 40.0f }, { 80.0f, 80.0f });

    mgr.selectArea(areaA, SelectionMode::HEXES, 0);
    const auto afterA = mgr.getCurrentSelection().getHexIndices();
    REQUIRE_FALSE(afterA.empty());

    // Alt+drag over a second area keeps the first selection and adds the new hexes.
    mgr.addArea(areaB, SelectionMode::HEXES, 0);
    const auto afterAdd = mgr.getCurrentSelection().getHexIndices();
    CHECK(afterAdd.size() > afterA.size());
    for (int hex : afterA) {
        CHECK(std::find(afterAdd.begin(), afterAdd.end(), hex) != afterAdd.end());
    }

    // Re-adding the same area is idempotent (no duplicates).
    mgr.addArea(areaB, SelectionMode::HEXES, 0);
    CHECK(mgr.getCurrentSelection().getHexIndices().size() == afterAdd.size());
}

// REGRESSION: a Ctrl+click on a selected tile must DESELECT it, and a Ctrl+click outside the
// selection must NOT add anything (deselect-only). The earlier toggle applied a fixed
// roof->object->floor priority and acted on whichever layer it found first, so in ALL mode
// with a roof above a selected floor it ADDED the roof instead of removing the floor — to the
// user, "Ctrl+click does nothing". It now removes whichever VISIBLE layer under the cursor is
// actually selected. The mock fabricates a roof+floor pair at every position, so the ALL-mode
// floor case reproduces "floor selected with a roof above it" exactly.
TEST_CASE("Ctrl+click deselects the selected layer only (single click)", "[selection_manager_real][regression]") {
    MockEditorWidget mockWidget;
    geck::selection::SelectionManager mgr(mockWidget);

    const sf::Vector2f clickPos{ 32.0f, 24.0f };

    SECTION("FLOOR_TILES: Ctrl+click on the selected tile deselects it") {
        mgr.selectAtPosition(clickPos, SelectionMode::FLOOR_TILES, 0);
        REQUIRE(mgr.getCurrentSelection().count() == 1);

        mgr.deselectAtPosition(clickPos, SelectionMode::FLOOR_TILES, 0);
        CHECK_FALSE(mgr.hasSelection());
    }

    SECTION("FLOOR_TILES: Ctrl+click outside the selection adds nothing") {
        // Nothing selected -> a deselect click must stay a no-op (no add).
        mgr.deselectAtPosition(clickPos, SelectionMode::FLOOR_TILES, 0);
        CHECK_FALSE(mgr.hasSelection());
    }

    SECTION("ALL: Ctrl+click on the selected roof deselects it") {
        mgr.selectAtPosition(clickPos, SelectionMode::ALL, 0); // selects roof (top of stack)
        REQUIRE(mgr.getCurrentSelection().getRoofTileIndices().size() == 1);

        mgr.deselectAtPosition(clickPos, SelectionMode::ALL, 0);
        CHECK_FALSE(mgr.hasSelection());
    }

    SECTION("ALL: Ctrl+click deselects a selected floor even with a roof above it") {
        // Cycle to the floor underneath (1st click roof, 2nd click floor).
        mgr.selectAtPosition(clickPos, SelectionMode::ALL, 0);
        mgr.selectAtPosition(clickPos, SelectionMode::ALL, 0);
        REQUIRE(mgr.getCurrentSelection().getFloorTileIndices().size() == 1);
        REQUIRE(mgr.getCurrentSelection().getRoofTileIndices().empty());

        mgr.deselectAtPosition(clickPos, SelectionMode::ALL, 0);
        // Must remove the floor, NOT add the roof on top of it.
        CHECK_FALSE(mgr.hasSelection());
    }

    SECTION("ALL: a hidden roof stays selected; only the visible floor is removed") {
        // Select both layers (roof while visible, then the floor underneath).
        mgr.selectAtPosition(clickPos, SelectionMode::ALL, 0); // roof
        mockWidget.roofVisible = false;                        // hide the roof, re-select adds floor
        mgr.addToSelection(clickPos, SelectionMode::FLOOR_TILES, 0);
        REQUIRE(mgr.getCurrentSelection().getRoofTileIndices().size() == 1);
        REQUIRE(mgr.getCurrentSelection().getFloorTileIndices().size() == 1);

        // Ctrl+click with the roof hidden must only drop the visible floor; the
        // hidden roof stays selected (deselecting a roof you can see is covered above).
        mgr.deselectAtPosition(clickPos, SelectionMode::ALL, 0);
        CHECK(mgr.getCurrentSelection().getFloorTileIndices().empty());
        CHECK(mgr.getCurrentSelection().getRoofTileIndices().size() == 1);
    }
}

//==============================================================================
// SECTION: Elevation regression for finishAreaSelection()
//
// REGRESSION TEST for the elevation bug fixed in the manager-decoupling work:
// SelectionManager::finishAreaSelection() used to call selectArea(area, mode, 0)
// with a HARDCODED elevation of 0, which broke area selection on every elevation
// other than the ground floor. It now calls selectArea(area, mode,
// _provider.getCurrentElevation()), and the roof-content filter in getTilesInArea()
// reads getMapFile().tiles.at(_provider.getCurrentElevation()).
//
// This test builds a populated spatial index and per-elevation roof data so that a
// ROOF area selection over a known tile yields the tile ONLY when the host reports
// the elevation where that roof actually exists. With the old hardcoded-0 behavior
// (or any regression that reads elevation 0 instead of the host's elevation), the
// elevation-2 selection below would come back EMPTY.
//
// Requires a graphics context to build the sprite-backed spatial index, so it is
// skipped in headless CI exactly like the other graphics tests in this suite.
//==============================================================================

TEST_CASE("SelectionManager area selection honors host elevation (regression)", "[selection_manager_real][regression]") {
    GECK_SKIP_IF_HEADLESS_CI();

    // The roof tile we will both place a sprite for and mark present on elevation 2.
    constexpr int TARGET_ROOF_TILE = 4242;

    // 1x1 texture shared by every tile sprite (bounds are what the spatial index uses).
    sf::Texture texture;
    sf::Image image{ sf::Vector2u{ 1, 1 }, sf::Color::White };
    if (!texture.loadFromImage(image)) {
        FAIL("Failed to create test texture");
    }

    MockEditorWidget mockWidget;

    // Build full floor/roof sprite vectors positioned at their isometric screen
    // coordinates, so TileSpatialIndex::buildIndex() can index every tile.
    mockWidget.floorSprites.reserve(TILES_PER_ELEVATION);
    mockWidget.roofSprites.reserve(TILES_PER_ELEVATION);
    for (int i = 0; i < TILES_PER_ELEVATION; ++i) {
        auto floorPos = indexToScreenPosition(i, false);
        sf::Sprite floorSprite(texture);
        floorSprite.setPosition({ static_cast<float>(floorPos.x), static_cast<float>(floorPos.y) });
        mockWidget.floorSprites.push_back(floorSprite);

        auto roofPos = indexToScreenPosition(i, true);
        sf::Sprite roofSprite(texture);
        roofSprite.setPosition({ static_cast<float>(roofPos.x), static_cast<float>(roofPos.y) });
        mockWidget.roofSprites.push_back(roofSprite);
    }

    // Per-elevation roof data:
    //   elevation 0: TARGET_ROOF_TILE roof is EMPTY (ground floor has nothing here)
    //   elevation 2: TARGET_ROOF_TILE roof is PRESENT
    mockWidget.seedElevation(0);
    mockWidget.seedElevation(2);
    mockWidget.setRoofPresent(2, TARGET_ROOF_TILE);

    geck::selection::SelectionManager mgr(mockWidget);
    mgr.initializeSpatialIndex();

    // Build a selection area tightly around the target roof sprite so the spatial
    // index returns it as a candidate.
    auto targetRoofPos = indexToScreenPosition(TARGET_ROOF_TILE, true);
    sf::Vector2f center(static_cast<float>(targetRoofPos.x), static_cast<float>(targetRoofPos.y));
    sf::Vector2f areaStart(center.x - 4.0f, center.y - 4.0f);
    sf::Vector2f areaEnd(center.x + 4.0f, center.y + 4.0f);

    SECTION("area selection on elevation 2 picks up the elevation-2 roof tile") {
        mockWidget.currentElevation = 2;

        mgr.startAreaSelection(areaStart, SelectionMode::ROOF_TILES);
        mgr.updateAreaSelection(areaEnd);
        // finishAreaSelection() forwards _provider.getCurrentElevation() (== 2) into
        // selectArea(); the roof-content filter then reads tiles.at(2).
        auto result = mgr.finishAreaSelection();

        REQUIRE(result.success);
        auto roofs = mgr.getCurrentSelection().getRoofTileIndices();
        // The target roof exists on elevation 2, so it must be selected. Had the code
        // read elevation 0 (the old hardcoded-0 bug), tiles.at(0) has it EMPTY and the
        // selection would be empty.
        REQUIRE_THAT(roofs, Catch::Matchers::VectorContains(TARGET_ROOF_TILE));
    }

    SECTION("same area on elevation 0 finds nothing (proves elevation is honored)") {
        mockWidget.currentElevation = 0;

        mgr.startAreaSelection(areaStart, SelectionMode::ROOF_TILES);
        mgr.updateAreaSelection(areaEnd);
        auto result = mgr.finishAreaSelection();

        REQUIRE(result.success);
        // On elevation 0 the target roof is EMPTY, so the roof-content filter excludes
        // it. This is the control case: combined with the elevation-2 section above it
        // proves finishAreaSelection() uses the host's elevation rather than a constant.
        REQUIRE(mgr.getCurrentSelection().getRoofTileIndices().empty());
    }

    SECTION("ALL mode skips a hidden roof; explicit ROOF_TILES still selects it (visibility parity)") {
        mockWidget.currentElevation = 2;
        mockWidget.roofVisible = false;
        const sf::FloatRect area({ areaStart.x, areaStart.y }, { areaEnd.x - areaStart.x, areaEnd.y - areaStart.y });

        // ALL mode must not grab a roof whose layer is hidden (you can't see it).
        REQUIRE(mgr.selectArea(area, SelectionMode::ALL, 2).success);
        auto allRoofs = mgr.getCurrentSelection().getRoofTileIndices();
        CHECK(std::find(allRoofs.begin(), allRoofs.end(), TARGET_ROOF_TILE) == allRoofs.end());

        // The explicit roof mode is a deliberate roof-editing mode, unaffected by the toggle.
        REQUIRE(mgr.selectArea(area, SelectionMode::ROOF_TILES, 2).success);
        auto roofModeRoofs = mgr.getCurrentSelection().getRoofTileIndices();
        CHECK(std::find(roofModeRoofs.begin(), roofModeRoofs.end(), TARGET_ROOF_TILE) != roofModeRoofs.end());
    }
}
