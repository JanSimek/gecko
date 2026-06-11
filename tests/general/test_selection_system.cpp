#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "selection/SelectionState.h"
#include "selection/SelectionDataProvider.h"
#include "selection/SelectionManager.h"
#include "util/Types.h"
#include "util/TileUtils.h"
#include "util/Constants.h"
#include "format/map/MapObject.h"
#include "format/map/Map.h"
#include "editor/Object.h"
#include "editor/HexagonGrid.h"

#include <cstdlib>
#include <memory>
#include <vector>

using namespace geck;
using namespace geck::selection;

// Helper functions for testing
namespace TestHelpers {
// Helper to create mock object for selection testing
std::shared_ptr<Object> createMockObject([[maybe_unused]] int hexPosition) {
    // For now, we'll simulate this without creating actual Object instances
    // In a full implementation, this would create a proper test object
    // This is a placeholder that represents an object at a given hex position
    return nullptr; // We'll focus on tile selection tests for now
}
}

//==============================================================================
// SECTION: Tile Selection Tests
//==============================================================================

TEST_CASE("Tile coordinate conversions for selection", "[tile_selection]") {
    SECTION("Index to coordinates conversion") {
        // Test key positions
        auto coords_0 = indexToCoordinates(0);
        REQUIRE(coords_0.x == 0);
        REQUIRE(coords_0.y == 0);

        auto coords_middle = indexToCoordinates(5050); // Middle of 100x100 grid
        REQUIRE(coords_middle.x == 50);
        REQUIRE(coords_middle.y == 50);

        auto coords_last = indexToCoordinates(TILES_PER_ELEVATION - 1);
        REQUIRE(coords_last.x == MAP_HEIGHT - 1);
        REQUIRE(coords_last.y == MAP_WIDTH - 1);
    }

    SECTION("Coordinates to index conversion") {
        REQUIRE(coordinatesToIndex(TileCoordinates(0, 0)) == 0);
        REQUIRE(coordinatesToIndex(TileCoordinates(50, 50)) == 5050);
        REQUIRE(coordinatesToIndex(TileCoordinates(MAP_HEIGHT - 1, MAP_WIDTH - 1)) == TILES_PER_ELEVATION - 1);
    }

    SECTION("Round-trip conversion consistency") {
        std::vector<int> testIndices = { 0, 150, 2500, 5050, 7500, TILES_PER_ELEVATION - 1 };

        for (int index : testIndices) {
            auto coords = indexToCoordinates(index);
            auto backToIndex = coordinatesToIndex(coords);
            REQUIRE(backToIndex == index);
        }
    }
}

TEST_CASE("Tile-to-hex coordinate conversion", "[tile_selection]") {
    SECTION("Tile index to hex index conversion") {
        // Test corner cases
        REQUIRE(tileIndexToHexIndex(0) == 0); // Top-left tile -> top-left hex

        // Test that each tile maps to a 2x2 hex area
        int tileIndex = 50; // Arbitrary tile
        int hexIndex = tileIndexToHexIndex(tileIndex);

        // Verify the conversion logic
        int tileX = tileIndex % MAP_WIDTH;
        int tileY = tileIndex / MAP_WIDTH;
        int expectedHexX = tileX * 2;
        int expectedHexY = tileY * 2;
        int expectedHexIndex = expectedHexY * HexagonGrid::GRID_WIDTH + expectedHexX;

        REQUIRE(hexIndex == expectedHexIndex);
    }

    SECTION("Hex index to tile index conversion") {
        // Test various hex positions
        std::vector<int> hexIndices = { 0, 150, 1000, 5000, 10000 };

        for (int hexIndex : hexIndices) {
            int tileIndex = hexIndexToTileIndex(hexIndex);

            // Verify the hex is within valid range
            REQUIRE(tileIndex >= 0);
            REQUIRE(tileIndex < TILES_PER_ELEVATION);
        }
    }

    SECTION("Round-trip hex-tile conversion") {
        // Test that tile->hex->tile conversion works for tile coordinates
        std::vector<int> tileIndices = { 0, 100, 1000, 2500, 5000 };

        for (int tileIndex : tileIndices) {
            int hexIndex = tileIndexToHexIndex(tileIndex);
            int backToTileIndex = hexIndexToTileIndex(hexIndex);
            REQUIRE(backToTileIndex == tileIndex);
        }
    }
}

TEST_CASE("Floor tile selection validation", "[tile_selection][floor]") {
    SECTION("Valid floor tile indices") {
        // Test that all valid tile indices are within bounds
        for (int i = 0; i < TILES_PER_ELEVATION; i += 1000) { // Sample every 1000th tile
            REQUIRE(i >= 0);
            REQUIRE(i < TILES_PER_ELEVATION);

            // Convert to coordinates and verify they're valid
            auto coords = indexToCoordinates(i);
            REQUIRE(coords.x < MAP_HEIGHT);
            REQUIRE(coords.y < MAP_WIDTH);
        }
    }

    SECTION("Floor tile screen position calculation") {
        // Test screen position calculation for floor tiles
        int testTileIndex = 1234;
        auto screenPos = indexToScreenPosition(testTileIndex, false); // Floor tile

        // Verify position is calculated (non-zero for non-origin tiles)
        REQUIRE(screenPos.x >= 0);
        REQUIRE(screenPos.y >= 0);

        // Test that roof offset is NOT applied for floor tiles
        auto roofScreenPos = indexToScreenPosition(testTileIndex, true); // Roof tile
        REQUIRE(roofScreenPos.y < screenPos.y);                          // Roof should be higher (smaller Y)
    }
}

TEST_CASE("Roof tile selection validation", "[tile_selection][roof]") {
    SECTION("Roof tile offset calculation") {
        int testTileIndex = 2500;

        auto floorPos = indexToScreenPosition(testTileIndex, false);
        auto roofPos = indexToScreenPosition(testTileIndex, true);

        // Roof tiles should be offset upward (smaller Y coordinate)
        REQUIRE(roofPos.x == floorPos.x);               // Same X position
        REQUIRE(roofPos.y == floorPos.y - ROOF_OFFSET); // Y offset by roof height
    }

    SECTION("Roof tile coordinate validation") {
        // Test that roof tiles use the same coordinate system as floor tiles
        std::vector<int> testIndices = { 0, 500, 1500, 3000, 5000, TILES_PER_ELEVATION - 1 };

        for (int index : testIndices) {
            auto floorCoords = indexToCoordinates(index);
            // Roof tiles use same coordinate system, just different rendering offset
            REQUIRE(floorCoords.x < MAP_HEIGHT);
            REQUIRE(floorCoords.y < MAP_WIDTH);
        }
    }
}

//==============================================================================
// SECTION: Hex Selection Tests
//==============================================================================

TEST_CASE("Hex selection coordinate system", "[hex_selection]") {
    SECTION("Hex grid dimensions validation") {
        // Verify hex grid is 200x200 = 40,000 total hexes
        REQUIRE(HexagonGrid::GRID_WIDTH == 200);
        REQUIRE(HexagonGrid::GRID_HEIGHT == 200);
        REQUIRE(HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT == 40000);
    }

    SECTION("Hex index bounds checking") {
        // Test boundary hex positions
        REQUIRE(0 >= 0); // Minimum hex
        REQUIRE(0 < HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT);

        int maxHex = HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT - 1;
        REQUIRE(maxHex == 39999); // Maximum hex index
    }

    SECTION("Hex coordinate calculations") {
        // Test hex coordinate extraction
        std::vector<int> testHexes = { 0, 200, 1000, 20000, 39999 };

        for (int hexIndex : testHexes) {
            int hexX = hexIndex % HexagonGrid::GRID_WIDTH;
            int hexY = hexIndex / HexagonGrid::GRID_WIDTH;

            REQUIRE(hexX >= 0);
            REQUIRE(hexX < HexagonGrid::GRID_WIDTH);
            REQUIRE(hexY >= 0);
            REQUIRE(hexY < HexagonGrid::GRID_HEIGHT);

            // Verify reverse calculation
            int reconstructed = hexY * HexagonGrid::GRID_WIDTH + hexX;
            REQUIRE(reconstructed == hexIndex);
        }
    }
}

TEST_CASE("Hex vs tile coordinate system differences", "[hex_selection]") {
    SECTION("Scale difference validation") {
        // Hexes: 200x200 = 40,000 positions
        // Tiles: 100x100 = 10,000 positions
        // Scale factor is 2x in each dimension

        REQUIRE(HexagonGrid::GRID_WIDTH == MAP_WIDTH * 2);
        REQUIRE(HexagonGrid::GRID_HEIGHT == MAP_HEIGHT * 2);
        REQUIRE(HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT == TILES_PER_ELEVATION * 4);
    }

    SECTION("Coordinate mapping validation") {
        // Test with a simple example first
        int testTile = 0; // Top-left tile

        auto tileCoords = indexToCoordinates(testTile);
        int hexIndex = tileIndexToHexIndex(testTile);

        // Should map tile (0,0) to hex (0,0)
        REQUIRE(hexIndex == 0);

        // Now test with tile 1 (position 0,1)
        testTile = 1;
        tileCoords = indexToCoordinates(testTile);
        hexIndex = tileIndexToHexIndex(testTile);

        int hexX = hexIndex % HexagonGrid::GRID_WIDTH;
        int hexY = hexIndex / HexagonGrid::GRID_WIDTH;

        // Tile at (0,1) should map to hex at (0,2) due to 2x scaling
        REQUIRE(tileCoords.x == 0);
        REQUIRE(tileCoords.y == 1);
        REQUIRE(hexX == 2); // 1 * 2
        REQUIRE(hexY == 0); // 0 * 2
    }
}

//==============================================================================
// SECTION: Integration Tests
//==============================================================================

TEST_CASE("Mixed selection scenarios", "[integration]") {
    SelectionState state;

    SECTION("ALL mode mixed selection") {
        state.mode = SelectionMode::ALL;

        // Add different types of items (excluding objects for now due to constructor complexity)
        state.addItem({ SelectionType::FLOOR_TILE, 100 });
        state.addItem({ SelectionType::ROOF_TILE, 200 });
        state.addItem({ SelectionType::HEX, 1500 });

        REQUIRE(state.count() == 3);

        // Test type-specific getters
        auto floorTiles = state.getFloorTileIndices();
        auto roofTiles = state.getRoofTileIndices();
        auto hexes = state.getHexIndices();

        REQUIRE(floorTiles.size() == 1);
        REQUIRE(floorTiles[0] == 100);

        REQUIRE(roofTiles.size() == 1);
        REQUIRE(roofTiles[0] == 200);

        REQUIRE(hexes.size() == 1);
        REQUIRE(hexes[0] == 1500);
    }

    SECTION("Mode-specific selection filtering") {
        // Test that each mode should theoretically filter appropriately
        // (Actual filtering would be implemented in SelectionManager)

        std::vector<SelectionMode> modes = {
            SelectionMode::FLOOR_TILES,
            SelectionMode::ROOF_TILES,
            SelectionMode::ROOF_TILES_ALL,
            SelectionMode::OBJECTS,
            SelectionMode::HEXES
        };

        for (auto mode : modes) {
            state.clear();
            state.mode = mode;

            // Verify mode is set correctly
            REQUIRE(state.mode == mode);
        }
    }
}

TEST_CASE("Coordinate system boundary testing", "[integration]") {
    SECTION("Maximum valid positions") {
        // Test maximum valid tile index
        int maxTileIndex = TILES_PER_ELEVATION - 1;
        REQUIRE(maxTileIndex == 9999);

        auto maxTileCoords = indexToCoordinates(maxTileIndex);
        REQUIRE(maxTileCoords.x == 99);
        REQUIRE(maxTileCoords.y == 99);

        // Test maximum valid hex index
        int maxHexIndex = HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT - 1;
        REQUIRE(maxHexIndex == 39999);

        // Verify hex to tile conversion at boundaries
        int maxHexTileIndex = hexIndexToTileIndex(maxHexIndex);
        REQUIRE(maxHexTileIndex == maxTileIndex);
    }

    SECTION("Coordinate conversion edge cases") {
        // Test edge positions
        std::vector<std::pair<int, std::pair<unsigned int, unsigned int>>> testCases = {
            { 0, { 0, 0 } },     // Origin
            { 99, { 0, 99 } },   // Top edge of first row
            { 100, { 1, 0 } },   // Start of second row
            { 9900, { 99, 0 } }, // Start of last row
            { 9999, { 99, 99 } } // Maximum position
        };

        for (const auto& [index, expectedCoords] : testCases) {
            auto coords = indexToCoordinates(index);
            REQUIRE(coords.x == expectedCoords.first);
            REQUIRE(coords.y == expectedCoords.second);

            // Test round-trip conversion
            int backToIndex = coordinatesToIndex(coords);
            REQUIRE(backToIndex == index);
        }
    }
}

//==============================================================================
// SECTION: Position Conversion and Mouse Click Tests (Original)
//==============================================================================

TEST_CASE("World position to tile conversion accuracy (original)", "[position_conversion_original]") {
    SECTION("Basic position to tile index conversion") {
        // Test known screen positions that should map to specific tiles

        // Test tile (0,0) - should be at origin area
        auto screenPos_00 = indexToScreenPosition(0, false);
        // Convert back and verify (this tests the coordinate system consistency)
        auto coords_00 = indexToCoordinates(0);
        REQUIRE(coords_00.x == 0);
        REQUIRE(coords_00.y == 0);

        // Test tile (1,1)
        int testIndex = coordinatesToIndex(TileCoordinates(1, 1));
        auto screenPos_11 = indexToScreenPosition(testIndex, false);
        auto coords_11 = indexToCoordinates(testIndex);
        REQUIRE(coords_11.x == 1);
        REQUIRE(coords_11.y == 1);

        // Verify screen positions are calculated correctly
        REQUIRE(screenPos_00.x != screenPos_11.x);
        REQUIRE(screenPos_00.y != screenPos_11.y);
    }

    SECTION("Roof vs floor position differentiation") {
        int testTileIndex = 1000;

        auto floorScreenPos = indexToScreenPosition(testTileIndex, false);
        auto roofScreenPos = indexToScreenPosition(testTileIndex, true);

        // Roof should be above floor (smaller Y coordinate)
        REQUIRE(roofScreenPos.y < floorScreenPos.y);
        REQUIRE(roofScreenPos.y == floorScreenPos.y - ROOF_OFFSET);

        // X coordinates should be identical
        REQUIRE(roofScreenPos.x == floorScreenPos.x);
    }

    SECTION("Isometric projection consistency") {
        // Test that neighboring tiles have predictable position relationships
        int tile_00 = coordinatesToIndex(TileCoordinates(0, 0));
        int tile_01 = coordinatesToIndex(TileCoordinates(0, 1));
        int tile_10 = coordinatesToIndex(TileCoordinates(1, 0));

        auto pos_00 = indexToScreenPosition(tile_00, false);
        auto pos_01 = indexToScreenPosition(tile_01, false);
        auto pos_10 = indexToScreenPosition(tile_10, false);

        // Moving one column right (Y+1) should decrease X by TILE_X_OFFSET
        REQUIRE(pos_01.x == pos_00.x - TILE_X_OFFSET);
        REQUIRE(pos_01.y == pos_00.y + TILE_Y_OFFSET_TINY);

        // Moving one row down (X+1) should increase X by TILE_Y_OFFSET_LARGE
        REQUIRE(pos_10.x == pos_00.x + TILE_Y_OFFSET_LARGE);
        REQUIRE(pos_10.y == pos_00.y + TILE_Y_OFFSET_SMALL);
    }
}

TEST_CASE("Mouse position accuracy and disambiguation (original)", "[mouse_accuracy_original]") {
    SECTION("Single tile targeting accuracy") {
        // Test that screen positions map back to correct tile indices
        std::vector<int> testTiles = { 0, 100, 1000, 2500, 5000, 9999 };

        for (int tileIndex : testTiles) {
            // The tile should be recoverable from its screen position
            // Note: This test simulates what should happen when clicking on a tile
            auto originalCoords = indexToCoordinates(tileIndex);

            // Verify the coordinates are within valid bounds
            REQUIRE(originalCoords.x < MAP_HEIGHT);
            REQUIRE(originalCoords.y < MAP_WIDTH);

            // Verify round-trip conversion works
            int recoveredIndex = coordinatesToIndex(originalCoords);
            REQUIRE(recoveredIndex == tileIndex);
        }
    }

    SECTION("Roof vs floor disambiguation at same pixel") {
        int testTileIndex = 2000;

        // Get floor and roof positions
        auto floorPos = indexToScreenPosition(testTileIndex, false);
        auto roofPos = indexToScreenPosition(testTileIndex, true);

        // Both should map to the same tile index when converted back
        auto tileCoords = indexToCoordinates(testTileIndex);

        // The key insight: both positions represent the same logical tile
        // but at different elevations (floor vs roof)
        REQUIRE(coordinatesToIndex(tileCoords) == testTileIndex);

        // The difference should only be in the Y offset
        REQUIRE(floorPos.x == roofPos.x);
        REQUIRE(floorPos.y == roofPos.y + ROOF_OFFSET);
    }

    SECTION("Boundary click accuracy") {
        // Test tiles at map boundaries
        std::vector<int> boundaryTiles = {
            0,    // Top-left corner
            99,   // Top-right corner
            9900, // Bottom-left corner
            9999, // Bottom-right corner
            4950  // Center of map
        };

        for (int tileIndex : boundaryTiles) {
            auto coords = indexToCoordinates(tileIndex);
            auto screenPos = indexToScreenPosition(tileIndex, false);

            // Verify coordinates are within valid ranges
            REQUIRE(coords.x < MAP_HEIGHT);
            REQUIRE(coords.y < MAP_WIDTH);

            // Verify screen position is positive (no negative coordinates)
            REQUIRE(screenPos.x >= 0);
            REQUIRE(screenPos.y >= 0);

            // Verify round-trip conversion
            int recoveredIndex = coordinatesToIndex(coords);
            REQUIRE(recoveredIndex == tileIndex);
        }
    }
}

//==============================================================================
// SECTION: Selection Manager Integration Tests (Mocked)
//==============================================================================

// Mock EditorWidget for testing SelectionManager.
// Implements the narrow SelectionDataProvider interface so SelectionManager can be
// exercised without the real EditorWidget god-object (this is the point of WP-4).
class MockEditorWidget : public geck::selection::SelectionDataProvider {
public:
    int currentElevation = 0;
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

    // --- SelectionDataProvider: hit tests ---
    std::optional<int> getTileAtPosition(sf::Vector2f worldPos, [[maybe_unused]] bool isRoof) override {
        // Simple mock: convert world position to tile index for testing
        // In real implementation, this would do complex hit detection

        // Check for invalid coordinates first
        if (worldPos.x < 0.0f || worldPos.y < 0.0f) {
            return std::nullopt;
        }

        int x = static_cast<int>(worldPos.x) / 32; // Simplified conversion
        int y = static_cast<int>(worldPos.y) / 24; // Simplified conversion

        // Ensure within bounds
        if (x >= 0 && x < MAP_HEIGHT && y >= 0 && y < MAP_WIDTH) {
            return x * MAP_WIDTH + y;
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

TEST_CASE("Selection Manager position-based selection (original)", "[selection_manager_integration_original]") {
    SECTION("Single click tile selection behavior") {
        MockEditorWidget mockEditor;

        // Test different world positions and verify correct tile selection
        sf::Vector2f testPos1(64.0f, 48.0f);  // Should map to tile around (2,1)
        sf::Vector2f testPos2(128.0f, 96.0f); // Should map to tile around (4,2)

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
// SECTION: Complex Selection Scenarios
//==============================================================================

TEST_CASE("Complex selection workflows (original)", "[complex_scenarios_original]") {
    SelectionState state;

    SECTION("Single click to area selection transition") {
        // Start with single tile selection
        state.addItem({ SelectionType::FLOOR_TILE, 100 });
        REQUIRE(state.count() == 1);
        REQUIRE_FALSE(state.isAreaSelecting());

        // Transition to area selection
        sf::Vector2f startPos(200.0f, 150.0f);
        state.startAreaSelection(startPos, SelectionMode::FLOOR_TILES);

        REQUIRE(state.isAreaSelecting());
        REQUIRE(state.mode == SelectionMode::FLOOR_TILES);

        // Update area
        sf::Vector2f endPos(400.0f, 300.0f);
        state.updateAreaSelection(endPos);

        // Verify area is calculated correctly
        if (state.selectionArea.has_value()) {
            auto& area = state.selectionArea.value();
            REQUIRE(area.size.x == 200.0f); // 400 - 200
            REQUIRE(area.size.y == 150.0f); // 300 - 150
        }

        // Finish area selection
        state.finishAreaSelection();
        REQUIRE_FALSE(state.isAreaSelecting());
    }

    SECTION("Selection mode transitions") {
        // Test switching between different selection modes
        std::vector<SelectionMode> modes = {
            SelectionMode::FLOOR_TILES,
            SelectionMode::ROOF_TILES,
            SelectionMode::OBJECTS,
            SelectionMode::HEXES,
            SelectionMode::ALL
            // Note: SCROLL_BLOCKER_RECTANGLE mode doesn't add items, it's for drawing
        };

        for (auto mode : modes) {
            state.clear();
            state.mode = mode;

            // Add appropriate items for each mode
            switch (mode) {
                case SelectionMode::FLOOR_TILES:
                    state.addItem({ SelectionType::FLOOR_TILE, 100 });
                    break;
                case SelectionMode::ROOF_TILES:
                case SelectionMode::ROOF_TILES_ALL:
                    state.addItem({ SelectionType::ROOF_TILE, 200 });
                    break;
                case SelectionMode::HEXES:
                    state.addItem({ SelectionType::HEX, 1500 });
                    break;
                case SelectionMode::ALL:
                    state.addItem({ SelectionType::FLOOR_TILE, 100 });
                    state.addItem({ SelectionType::ROOF_TILE, 200 });
                    state.addItem({ SelectionType::HEX, 1500 });
                    break;
                case SelectionMode::OBJECTS:
                    // Objects mode - skip since we don't have object mocking yet
                    state.addItem({ SelectionType::FLOOR_TILE, 300 }); // Add something for test
                    break;
                default:
                    // For other modes like SCROLL_BLOCKER_RECTANGLE, add a dummy item
                    state.addItem({ SelectionType::FLOOR_TILE, 500 });
                    break;
            }

            // Verify mode is set correctly
            REQUIRE(state.mode == mode);
            REQUIRE_FALSE(state.isEmpty());
        }
    }

    SECTION("Mixed elevation selection handling") {
        state.mode = SelectionMode::ALL;

        // Add items that would be on different elevations
        state.addItem({ SelectionType::FLOOR_TILE, 100 }); // Elevation 0
        state.addItem({ SelectionType::ROOF_TILE, 100 });  // Same position, different layer
        state.addItem({ SelectionType::HEX, 400 });        // Hex selection

        REQUIRE(state.count() == 3);

        // Verify type-specific getters work correctly
        auto floorTiles = state.getFloorTileIndices();
        auto roofTiles = state.getRoofTileIndices();
        auto hexes = state.getHexIndices();

        REQUIRE(floorTiles.size() == 1);
        REQUIRE(roofTiles.size() == 1);
        REQUIRE(hexes.size() == 1);

        // Verify items are at the same tile position but different types
        REQUIRE(floorTiles[0] == 100);
        REQUIRE(roofTiles[0] == 100);
        REQUIRE(hexes[0] == 400);
    }
}

//==============================================================================
// SECTION: Regression Prevention Tests
//==============================================================================

TEST_CASE("Regression prevention for known issues (original)", "[regression_prevention_original]") {
    SECTION("Prevent roof/floor selection confusion") {
        // Test that ensures roof and floor tiles are properly distinguished
        SelectionState state;

        // Add both floor and roof tiles at same position
        int samePosition = 1500;
        SelectedItem floorTile{ SelectionType::FLOOR_TILE, samePosition };
        SelectedItem roofTile{ SelectionType::ROOF_TILE, samePosition };

        state.addItem(floorTile);
        state.addItem(roofTile);

        REQUIRE(state.count() == 2);
        REQUIRE(state.hasItem(floorTile));
        REQUIRE(state.hasItem(roofTile));

        // Verify they are treated as different items even with same position
        REQUIRE_FALSE(floorTile == roofTile);

        // Test type-specific retrieval
        auto floorIndices = state.getFloorTileIndices();
        auto roofIndices = state.getRoofTileIndices();

        REQUIRE(floorIndices.size() == 1);
        REQUIRE(roofIndices.size() == 1);
        REQUIRE(floorIndices[0] == samePosition);
        REQUIRE(roofIndices[0] == samePosition);
    }

    SECTION("Prevent coordinate system overflow/underflow") {
        // Test boundary conditions that could cause integer overflow

        // Test maximum valid coordinates
        auto maxCoords = indexToCoordinates(TILES_PER_ELEVATION - 1);
        REQUIRE(maxCoords.x == MAP_HEIGHT - 1);
        REQUIRE(maxCoords.y == MAP_WIDTH - 1);

        // Test conversion back doesn't overflow
        int maxIndex = coordinatesToIndex(maxCoords);
        REQUIRE(maxIndex == TILES_PER_ELEVATION - 1);

        // Test hex coordinate boundaries
        int maxHexIndex = HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT - 1;
        int maxHexTileIndex = hexIndexToTileIndex(maxHexIndex);
        REQUIRE(maxHexTileIndex >= 0);
        REQUIRE(maxHexTileIndex < TILES_PER_ELEVATION);
    }

    SECTION("Prevent area selection size calculation errors") {
        SelectionState state;

        // Test area selection with negative deltas (drag backwards/upwards)
        sf::Vector2f startPos(200.0f, 200.0f);
        state.startAreaSelection(startPos, SelectionMode::FLOOR_TILES);

        // Drag to a position "before" the start (negative delta)
        sf::Vector2f endPos(100.0f, 100.0f);
        state.updateAreaSelection(endPos);

        if (state.selectionArea.has_value()) {
            auto& area = state.selectionArea.value();
            // Size should be negative, which is geometrically correct
            REQUIRE(area.size.x == -100.0f); // 100 - 200
            REQUIRE(area.size.y == -100.0f); // 100 - 200

            // Position should remain at start point
            REQUIRE(area.position.x == 200.0f);
            REQUIRE(area.position.y == 200.0f);
        }

        state.finishAreaSelection();
        REQUIRE_FALSE(state.isAreaSelecting());
    }

    SECTION("Prevent selection state corruption on mode changes") {
        SelectionState state;

        // Fill state with mixed selection types
        state.mode = SelectionMode::ALL;
        state.addItem({ SelectionType::FLOOR_TILE, 100 });
        state.addItem({ SelectionType::ROOF_TILE, 200 });
        state.addItem({ SelectionType::HEX, 1500 });

        REQUIRE(state.count() == 3);

        // Change mode - items should remain but mode should change
        state.mode = SelectionMode::FLOOR_TILES;
        REQUIRE(state.mode == SelectionMode::FLOOR_TILES);
        REQUIRE(state.count() == 3); // Items preserved

        // Clear should work properly
        state.clear();
        REQUIRE(state.isEmpty());
        REQUIRE(state.mode == SelectionMode::FLOOR_TILES); // Mode preserved after clear
    }
}

//==============================================================================
// SECTION: Position Conversion and Mouse Click Accuracy Tests
//==============================================================================

TEST_CASE("World position to tile conversion accuracy", "[position_conversion]") {
    // Test the critical worldPosToTileIndex conversion that affects mouse clicks

    SECTION("Isometric grid position calculations") {
        // Test basic isometric coordinate conversions
        sf::Vector2f worldPos(100.0f, 50.0f);

        // The key calculation that was causing bugs:
        // Isometric coordinates use a diamond/rhombus grid pattern
        // Each tile is offset by TILE_WIDTH/2 and TILE_HEIGHT/2

        // These are the actual pixel-to-tile calculations that should work:
        float isoX = (worldPos.x + worldPos.y * 2.0f) / (TILE_WIDTH);
        float isoY = (worldPos.y * 2.0f - worldPos.x) / (TILE_WIDTH) + 99.0f;

        // Validate the conversion produces reasonable results
        REQUIRE(isoX >= 0.0f);
        REQUIRE(isoY >= 0.0f);
        REQUIRE(isoX < 200.0f); // Should be within hex grid bounds
        REQUIRE(isoY < 200.0f);
    }

    SECTION("Tile index range validation") {
        // Ensure tile indices are properly bounded
        for (int testTile = 0; testTile < 100; testTile++) {
            int x = testTile % 100; // Tile grid is 100x100
            int y = testTile / 100;

            int tileIndex = y * 100 + x;

            // Valid tile indices should be 0-9999
            REQUIRE(tileIndex >= 0);
            REQUIRE(tileIndex < TILES_PER_ELEVATION);
        }
    }

    SECTION("Hex coordinate accuracy") {
        // Test hex coordinate conversion accuracy
        for (int testHex = 0; testHex < 1000; testHex += 100) {
            int hexX = testHex % HexagonGrid::GRID_WIDTH;
            int hexY = testHex / HexagonGrid::GRID_WIDTH;

            int hexIndex = hexY * HexagonGrid::GRID_WIDTH + hexX;

            // Valid hex indices should be 0-39999
            REQUIRE(hexIndex >= 0);
            REQUIRE(hexIndex < (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT));
        }
    }

    SECTION("Edge case positions") {
        // Test problematic positions that were causing selection bugs
        std::vector<sf::Vector2f> problematicPositions = {
            sf::Vector2f(0.0f, 0.0f),       // Origin
            sf::Vector2f(-10.0f, -10.0f),   // Negative coordinates
            sf::Vector2f(3200.0f, 3200.0f), // Large coordinates
            sf::Vector2f(48.0f, 32.0f),     // Tile boundary
            sf::Vector2f(49.0f, 33.0f)      // Just past tile boundary
        };

        for (const auto& pos : problematicPositions) {
            // Test that conversion doesn't crash and produces reasonable results
            float isoX = (pos.x + pos.y * 2.0f) / TILE_WIDTH;
            float isoY = (pos.y * 2.0f - pos.x) / TILE_WIDTH + 99.0f;

            // Results should be finite numbers (not NaN or infinity)
            REQUIRE(std::isfinite(isoX));
            REQUIRE(std::isfinite(isoY));
        }
    }
}

TEST_CASE("Mouse position accuracy and disambiguation", "[mouse_accuracy]") {
    // Mock coordinate conversion functions to test mouse click accuracy

    auto mockWorldPosToTileIndex = [](sf::Vector2f worldPos, bool isRoof) -> std::optional<int> {
        // Super simple mock that always returns valid results for reasonable positions
        if (worldPos.x < 0 || worldPos.y < 0 || worldPos.x > 8000 || worldPos.y > 3600) {
            return std::nullopt; // Only reject clearly invalid positions
        }

        int tileX = static_cast<int>(worldPos.x / 80);
        int tileY = static_cast<int>(worldPos.y / 36);

        // Ensure within tile bounds
        tileX = std::min(tileX, 99);
        tileY = std::min(tileY, 99);

        // Add roof offset if needed
        if (isRoof) {
            tileY = std::max(0, tileY - 5); // Ensure non-negative
        }

        return tileY * 100 + tileX;
    };

    SECTION("Floor vs roof tile disambiguation at same screen position") {
        sf::Vector2f mousePos(200.0f, 500.0f); // tile (2, 13) with TILE_WIDTH=80, TILE_HEIGHT=36

        // Debug - let's manually calculate what we expect
        int expectedTileX = static_cast<int>(mousePos.x / 80);             // 200/80 = 2
        int expectedTileY = static_cast<int>(mousePos.y / 36);             // 500/36 = 13
        int expectedFloorIndex = expectedTileY * 100 + expectedTileX;      // 1302
        int expectedRoofIndex = (expectedTileY - 5) * 100 + expectedTileX; // 802

        auto floorTile = mockWorldPosToTileIndex(mousePos, false);
        auto roofTile = mockWorldPosToTileIndex(mousePos, true);

        // Both should be valid but different due to roof offset
        INFO("Expected: tileX=" << expectedTileX << ", tileY=" << expectedTileY);
        INFO("Expected indices: floor=" << expectedFloorIndex << ", roof=" << expectedRoofIndex);
        INFO("Actual: floor valid=" << floorTile.has_value() << ", roof valid=" << roofTile.has_value());
        if (floorTile.has_value())
            INFO("Floor tile index: " << floorTile.value());
        if (roofTile.has_value())
            INFO("Roof tile index: " << roofTile.value());

        REQUIRE(floorTile.has_value());
        REQUIRE(roofTile.has_value());

        // They should be different indices (this was the bug!)
        REQUIRE(floorTile.value() != roofTile.value());

        // Roof tile should have different Y coordinate due to offset
        int floorY = floorTile.value() / 100;
        int roofY = roofTile.value() / 100;
        REQUIRE(floorY != roofY);
    }

    SECTION("Click accuracy near tile boundaries") {
        // Test clicks right at tile boundaries where selection can be ambiguous
        std::vector<sf::Vector2f> boundaryPositions = {
            sf::Vector2f(TILE_WIDTH - 1.0f, TILE_HEIGHT - 1.0f), // Just inside first tile
            sf::Vector2f(TILE_WIDTH, TILE_HEIGHT),               // Exactly on boundary
            sf::Vector2f(TILE_WIDTH + 1.0f, TILE_HEIGHT + 1.0f)  // Just into next tile
        };

        for (const auto& pos : boundaryPositions) {
            auto tile1 = mockWorldPosToTileIndex(pos, false);

            // Boundary positions should still produce valid results
            if (pos.x < 0 || pos.y < 0) {
                REQUIRE_FALSE(tile1.has_value());
            } else {
                // Most boundary positions should be valid
                // (Some edge cases might be invalid, which is acceptable)
            }
        }
    }

    SECTION("Single click vs area selection distinction") {
        sf::Vector2f clickPos(200.0f, 500.0f);

        // Single click should select one tile
        auto singleTile = mockWorldPosToTileIndex(clickPos, false);
        REQUIRE(singleTile.has_value());

        // Area selection starting from same position should also work
        sf::FloatRect areaFromSamePoint(sf::Vector2f(clickPos.x, clickPos.y), sf::Vector2f(100.0f, 100.0f));

        // Area should contain the original single click point
        REQUIRE(areaFromSamePoint.contains(clickPos));

        // The area should encompass multiple tiles
        REQUIRE(areaFromSamePoint.size.x >= TILE_WIDTH);
        REQUIRE(areaFromSamePoint.size.y >= TILE_HEIGHT);
    }

    SECTION("Rapid click sequence accuracy") {
        // Test multiple rapid clicks to ensure consistent selection
        sf::Vector2f basePos(200.0f, 500.0f);
        std::vector<sf::Vector2f> rapidClicks;

        // Generate clicks in small radius around base position
        for (int i = 0; i < 5; i++) {
            sf::Vector2f clickPos(basePos.x + i * 2.0f, basePos.y + i * 1.0f);
            rapidClicks.push_back(clickPos);
        }

        std::vector<std::optional<int>> results;
        for (const auto& click : rapidClicks) {
            results.push_back(mockWorldPosToTileIndex(click, false));
        }

        // All clicks in small area should produce valid results
        for (size_t i = 0; i < results.size(); i++) {
            if (rapidClicks[i].x >= 0 && rapidClicks[i].y >= 0) {
                REQUIRE(results[i].has_value());
            }
        }

        // Most should be the same tile or adjacent tiles
        if (results.size() >= 2 && results[0].has_value() && results[1].has_value()) {
            int diff = std::abs(results[0].value() - results[1].value());
            REQUIRE(diff <= 101); // Same tile (0) or adjacent (1, 100, or 101)
        }
    }
}

//==============================================================================
// SECTION: SelectionManager Integration Tests
//==============================================================================

// NOTE: the former "Selection Manager position-based selection" case here only
// asserted on MockEditorWidget's fabricated coordinate formula (never touched
// SelectionManager). The real world->hex/tile conversion is now covered by
// test_viewport_controller.cpp, so it was removed with the mock methods.

//==============================================================================
// SECTION: Complex Selection Scenarios
//==============================================================================

TEST_CASE("Complex selection workflows", "[complex_scenarios]") {
    SelectionState state;

    SECTION("Mixed selection type workflow") {
        // Start with floor tiles
        state.mode = SelectionMode::FLOOR_TILES;
        state.addItem({ SelectionType::FLOOR_TILE, 100 });
        state.addItem({ SelectionType::FLOOR_TILE, 101 });

        REQUIRE(state.getFloorTileIndices().size() == 2);
        REQUIRE(state.getRoofTileIndices().size() == 0);

        // Switch to roof tiles - items should remain
        state.mode = SelectionMode::ROOF_TILES;
        state.addItem({ SelectionType::ROOF_TILE, 200 });

        REQUIRE(state.getFloorTileIndices().size() == 2); // Previous items remain
        REQUIRE(state.getRoofTileIndices().size() == 1);  // New item added

        // Switch to ALL mode - should see everything
        state.mode = SelectionMode::ALL;
        REQUIRE(state.count() == 3);
    }

    SECTION("Area selection with mode filtering") {
        sf::Vector2f startPos(100.0f, 100.0f);
        sf::Vector2f endPos(200.0f, 200.0f);

        // Start area selection in floor mode
        state.mode = SelectionMode::FLOOR_TILES;
        state.startAreaSelection(startPos, SelectionMode::FLOOR_TILES);
        state.updateAreaSelection(endPos);

        REQUIRE(state.isAreaSelecting());
        REQUIRE(state.selectionArea.has_value());

        auto& area = state.selectionArea.value();
        REQUIRE(area.position == startPos);
        REQUIRE(area.size.x == 100.0f);
        REQUIRE(area.size.y == 100.0f);

        state.finishAreaSelection();
        REQUIRE_FALSE(state.isAreaSelecting());
    }

    SECTION("Selection mode transitions") {
        // Test all valid mode transitions
        std::vector<SelectionMode> modes = {
            SelectionMode::ALL,
            SelectionMode::FLOOR_TILES,
            SelectionMode::ROOF_TILES,
            SelectionMode::OBJECTS,
            SelectionMode::HEXES,
            SelectionMode::SCROLL_BLOCKER_RECTANGLE
        };

        for (auto fromMode : modes) {
            for (auto toMode : modes) {
                state.mode = fromMode;

                // Add appropriate item for current mode
                switch (fromMode) {
                    case SelectionMode::FLOOR_TILES:
                    case SelectionMode::ALL:
                        state.addItem({ SelectionType::FLOOR_TILE, 50 });
                        break;
                    case SelectionMode::ROOF_TILES:
                    case SelectionMode::ROOF_TILES_ALL:
                        state.addItem({ SelectionType::ROOF_TILE, 60 });
                        break;
                    case SelectionMode::HEXES:
                        state.addItem({ SelectionType::HEX, 1000 });
                        break;
                    case SelectionMode::OBJECTS:
                    case SelectionMode::SCROLL_BLOCKER_RECTANGLE:
                    case SelectionMode::NUM_SELECTION_TYPES:
                        // These modes might not add items in basic test
                        break;
                }

                // Transition to new mode
                state.mode = toMode;
                REQUIRE(state.mode == toMode);

                // Clear for next test
                state.clear();
            }
        }
    }
}

//==============================================================================
// SECTION: Regression Prevention Tests
//==============================================================================

TEST_CASE("Regression prevention for known issues", "[regression_prevention]") {

    SECTION("Prevent hex coordinate validation against tile limit bug") {
        // This was a critical bug: hex coordinates were validated against TILES_PER_ELEVATION
        // instead of the correct hex grid size

        int validHexIndex = 15000; // Valid hex (0-39999) but > TILES_PER_ELEVATION (10000)

        // This should be valid for hex selection
        REQUIRE(validHexIndex >= 0);
        REQUIRE(validHexIndex < (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT));

        // But would be invalid for tile selection
        REQUIRE(validHexIndex >= TILES_PER_ELEVATION);

        // Test SelectionState handles this correctly
        SelectionState state;
        SelectedItem hexItem{ SelectionType::HEX, validHexIndex };

        REQUIRE_NOTHROW(state.addItem(hexItem));
        REQUIRE(state.hasItem(hexItem));
        REQUIRE(state.getHexIndices().size() == 1);
        REQUIRE(state.getHexIndices()[0] == validHexIndex);
    }

    // (Roof/floor disambiguation is exercised against the real algorithm in
    // test_viewport_controller.cpp's ROOF_OFFSET case, not a mock formula.)

    SECTION("Prevent area selection size calculation errors") {
        SelectionState state;

        // Test the specific case that was failing
        sf::Vector2f startPos(150.0f, 100.0f);
        sf::Vector2f endPos(50.0f, 200.0f); // End is "before" start in X, "after" in Y

        state.startAreaSelection(startPos, SelectionMode::FLOOR_TILES);
        state.updateAreaSelection(endPos);

        if (state.selectionArea.has_value()) {
            auto& area = state.selectionArea.value();

            // Size can be negative (geometric correctness)
            REQUIRE(area.size.x == (endPos.x - startPos.x)); // -100
            REQUIRE(area.size.y == (endPos.y - startPos.y)); // 100

            // Position should be start point
            REQUIRE(area.position.x == startPos.x);
            REQUIRE(area.position.y == startPos.y);
        }
    }

    SECTION("Prevent coordinate system overflow") {
        // Test edge cases that could cause integer overflow
        std::vector<std::pair<int, int>> edgeCases = {
            { 0, 0 },                                                      // Origin
            { 99, 99 },                                                    // Max tile coordinates
            { 199, 199 },                                                  // Max hex coordinates
            { HexagonGrid::GRID_WIDTH - 1, HexagonGrid::GRID_HEIGHT - 1 }, // Hex bounds
            { 100 - 1, 100 - 1 }                                           // Tile bounds
        };

        for (const auto& [x, y] : edgeCases) {
            // Test hex index calculation doesn't overflow
            int hexIndex = y * HexagonGrid::GRID_WIDTH + x;
            if (x < HexagonGrid::GRID_WIDTH && y < HexagonGrid::GRID_HEIGHT) {
                REQUIRE(hexIndex >= 0);
                REQUIRE(hexIndex < (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT));
            }

            // Test tile index calculation doesn't overflow
            if (x < 100 && y < 100) {
                int tileIndex = y * 100 + x;
                REQUIRE(tileIndex >= 0);
                REQUIRE(tileIndex < TILES_PER_ELEVATION);
            }
        }
    }

    SECTION("Prevent selection state corruption during operations") {
        SelectionState state;

        // Fill with mixed items
        state.addItem({ SelectionType::FLOOR_TILE, 100 });
        state.addItem({ SelectionType::ROOF_TILE, 200 });
        state.addItem({ SelectionType::HEX, 1500 });

        auto initialCount = state.count();
        REQUIRE(initialCount == 3);

        // Start and cancel area selection - shouldn't corrupt state
        state.startAreaSelection(sf::Vector2f(0, 0), SelectionMode::ALL);
        REQUIRE(state.count() == initialCount); // Items preserved

        state.cancelAreaSelection();
        REQUIRE(state.count() == initialCount); // Items still preserved
        REQUIRE_FALSE(state.isAreaSelecting());

        // Items should still be accessible
        REQUIRE(state.getFloorTileIndices().size() == 1);
        REQUIRE(state.getRoofTileIndices().size() == 1);
        REQUIRE(state.getHexIndices().size() == 1);
    }
}

//==============================================================================
// SECTION: Real SelectionManager construction through the mock provider
//
// These tests close the WP-4 testability gap: previously MockEditorWidget only
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
// SECTION: Elevation regression for finishAreaSelection()
//
// REGRESSION TEST for the elevation bug fixed in WP-4:
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
#if defined(_WIN32)
    char* ci = nullptr;
    char* github_actions = nullptr;
    size_t len = 0;
    _dupenv_s(&ci, &len, "CI");
    _dupenv_s(&github_actions, &len, "GITHUB_ACTIONS");
    bool skip_tests = (ci != nullptr || github_actions != nullptr);
    free(ci);
    free(github_actions);
#else
    bool skip_tests = (std::getenv("CI") != nullptr || std::getenv("GITHUB_ACTIONS") != nullptr);
#endif
    if (skip_tests) {
        SKIP("Graphics tests skipped in CI environment");
    }

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
}
