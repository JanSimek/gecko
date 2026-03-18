#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "editor/HexagonGrid.h"

using namespace geck;

TEST_CASE("HexagonGrid validates positions and converts to tiles", "[hex_grid]") {
    HexagonGrid grid;

    SECTION("position bounds reflect the grid size") {
        REQUIRE(grid.size() == HexagonGrid::POSITION_COUNT);
        REQUIRE(grid.containsPosition(0));
        REQUIRE(grid.containsPosition(HexagonGrid::POSITION_COUNT - 1));
        REQUIRE_FALSE(grid.containsPosition(-1));
        REQUIRE_FALSE(grid.containsPosition(HexagonGrid::POSITION_COUNT));
    }

    SECTION("neighboring 2x2 hex cells map to the same tile") {
        REQUIRE(grid.tileIndexForPosition(0) == 0);
        REQUIRE(grid.tileIndexForPosition(1) == 0);
        REQUIRE(grid.tileIndexForPosition(HexagonGrid::GRID_WIDTH) == 0);
        REQUIRE(grid.tileIndexForPosition(HexagonGrid::GRID_WIDTH + 1) == 0);
        REQUIRE(grid.tileIndexForPosition(2) == 1);
        REQUIRE(grid.tileIndexForPosition(HexagonGrid::POSITION_COUNT - 1) == HexagonGrid::TILE_COUNT - 1);
    }
}

TEST_CASE("HexagonGrid rectangleBorderPositions returns the outer ring", "[hex_grid]") {
    HexagonGrid grid;

    const auto border = grid.rectangleBorderPositions(
        0,
        2,
        HexagonGrid::GRID_WIDTH * 2,
        HexagonGrid::GRID_WIDTH * 2 + 2);

    REQUIRE_THAT(border, Catch::Matchers::Equals(std::vector<int>{
                             0,
                             1,
                             2,
                             HexagonGrid::GRID_WIDTH,
                             HexagonGrid::GRID_WIDTH + 2,
                             HexagonGrid::GRID_WIDTH * 2,
                             HexagonGrid::GRID_WIDTH * 2 + 1,
                             HexagonGrid::GRID_WIDTH * 2 + 2,
                         }));
}
