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

TEST_CASE("HexagonGrid position<->coordinates round-trip over the whole grid", "[hex_grid]") {
    HexagonGrid grid;

    // Row-major mapping: position = y * GRID_WIDTH + x.
    REQUIRE(grid.coordinatesForPosition(0).has_value());
    CHECK(grid.coordinatesForPosition(0)->x == 0);
    CHECK(grid.coordinatesForPosition(0)->y == 0);
    CHECK(grid.coordinatesForPosition(HexagonGrid::GRID_WIDTH + 1)->x == 1);
    CHECK(grid.coordinatesForPosition(HexagonGrid::GRID_WIDTH + 1)->y == 1);
    CHECK(grid.coordinatesForPosition(HexagonGrid::POSITION_COUNT - 1)->x == HexagonGrid::GRID_WIDTH - 1);
    CHECK(grid.coordinatesForPosition(HexagonGrid::POSITION_COUNT - 1)->y == HexagonGrid::GRID_HEIGHT - 1);

    // Every position converts to coordinates and back to the same position.
    for (int position = 0; position < HexagonGrid::POSITION_COUNT; ++position) {
        const auto coords = grid.coordinatesForPosition(position);
        REQUIRE(coords.has_value());
        const auto roundTripped = grid.positionForCoordinates(coords->x, coords->y);
        REQUIRE(roundTripped.has_value());
        REQUIRE(*roundTripped == position);
    }
}

TEST_CASE("HexagonGrid rejects out-of-range positions and coordinates", "[hex_grid]") {
    HexagonGrid grid;

    CHECK_FALSE(grid.coordinatesForPosition(-1).has_value());
    CHECK_FALSE(grid.coordinatesForPosition(HexagonGrid::POSITION_COUNT).has_value());
    CHECK_FALSE(grid.tileIndexForPosition(HexagonGrid::POSITION_COUNT).has_value());
    CHECK_FALSE(grid.getHexByPosition(HexagonGrid::POSITION_COUNT).has_value());

    CHECK_FALSE(grid.positionForCoordinates(-1, 0).has_value());
    CHECK_FALSE(grid.positionForCoordinates(HexagonGrid::GRID_WIDTH, 0).has_value());
    CHECK_FALSE(grid.positionForCoordinates(0, HexagonGrid::GRID_HEIGHT).has_value());
}

TEST_CASE("HexagonGrid positionAt resolves a screen point to the hex sitting there", "[hex_grid]") {
    HexagonGrid grid;

    // A hex's own logical (x, y) must resolve back to a hex at that exact point
    // (nearest-hex stability), independent of any coordinate ties.
    for (int position : { 0, 1, HexagonGrid::GRID_WIDTH, 12345, HexagonGrid::POSITION_COUNT - 1 }) {
        const auto hex = grid.getHexByPosition(static_cast<uint32_t>(position));
        REQUIRE(hex.has_value());
        const Hex& h = hex->get();

        const uint32_t found = grid.positionAt(static_cast<uint32_t>(h.x()), static_cast<uint32_t>(h.y()));
        REQUIRE(found != Hex::HEX_OUT_OF_MAP);
        const auto foundHex = grid.getHexByPosition(found);
        REQUIRE(foundHex.has_value());
        CHECK(foundHex->get().x() == h.x());
        CHECK(foundHex->get().y() == h.y());
    }

    // A point far outside the grid resolves to no hex.
    CHECK(grid.positionAt(100000, 100000) == Hex::HEX_OUT_OF_MAP);
}
