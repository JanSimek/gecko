#include <catch2/catch_test_macros.hpp>

#include "editor/HexagonGrid.h"
#include "util/Constants.h"
#include "util/TileUtils.h"

using namespace geck;

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
        // A non-origin tile must map to a screen position distinct from the origin.
        int testTileIndex = 1234;
        auto screenPos = indexToScreenPosition(testTileIndex, false); // Floor tile
        auto originPos = indexToScreenPosition(0, false);

        REQUIRE((screenPos.x != originPos.x || screenPos.y != originPos.y));

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
        // The minimum hex index (0) must fall within the grid bounds.
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
