#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "editor/HexagonGrid.h"
#include "editor/HexGeometry.h"
#include "util/TileUtils.h"

#include <algorithm>
#include <set>
#include <vector>

using namespace geck;

TEST_CASE("HexagonGrid validates positions and converts to tiles", "[hexagon_grid]") {
    HexagonGrid grid;

    SECTION("position bounds reflect the grid size") {
        REQUIRE(grid.size() == HexagonGrid::POSITION_COUNT);
        REQUIRE(hexgrid::isValidPosition(0));
        REQUIRE(hexgrid::isValidPosition(HexagonGrid::POSITION_COUNT - 1));
        REQUIRE_FALSE(hexgrid::isValidPosition(-1));
        REQUIRE_FALSE(hexgrid::isValidPosition(HexagonGrid::POSITION_COUNT));
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

TEST_CASE("tileIndexToHexIndex picks a tile's top-left hex and round-trips through tileIndexForPosition", "[hexagon_grid]") {
    const HexagonGrid grid;

    CHECK(tileIndexToHexIndex(0) == 0);
    CHECK(tileIndexToHexIndex(1) == 2);
    CHECK(tileIndexToHexIndex(50) == 100);
    CHECK(tileIndexToHexIndex(HexagonGrid::TILE_GRID_WIDTH) == 2 * HexagonGrid::GRID_WIDTH);
    CHECK(tileIndexToHexIndex(HexagonGrid::TILE_COUNT - 1) == 39798); // tile (99, 99) -> hex (198, 198)

    for (const int tileIndex : { 0, 100, 1000, 2500, 5000, HexagonGrid::TILE_COUNT - 1 }) {
        CHECK(grid.tileIndexForPosition(tileIndexToHexIndex(tileIndex)) == tileIndex);
    }
}

namespace {
struct ScreenRect {
    float left;
    float top;
    float right;
    float bottom;

    [[nodiscard]] bool contains(const Hex& hex) const {
        const auto x = static_cast<float>(hex.x());
        const auto y = static_cast<float>(hex.y());
        return x >= left && x <= right && y >= top && y <= bottom;
    }

    // Neighbouring hex centres are at most 32 px apart across and 12 px down, so a hex on the inside
    // edge lies that close to a side.
    [[nodiscard]] bool hugsAnEdge(const Hex& hex) const {
        const auto x = static_cast<float>(hex.x());
        const auto y = static_cast<float>(hex.y());
        return x - left < 32.f || right - x < 32.f || y - top < 12.f || bottom - y < 12.f;
    }
};

// The hexes of `region` reachable from `start` stepping between hex neighbours inside `region`.
std::set<int> connectedWithin(const std::set<int>& region, int start) {
    std::set reached{ start };
    std::vector frontier{ start };
    while (!frontier.empty()) {
        const int position = frontier.back();
        frontier.pop_back();
        for (int neighbour : hexgrid::neighbors(position)) {
            if (region.contains(neighbour) && reached.insert(neighbour).second) {
                frontier.push_back(neighbour);
            }
        }
    }
    return reached;
}
} // namespace

TEST_CASE("HexagonGrid screenRectangleBorderPositions follows the screen rectangle", "[hexagon_grid]") {
    HexagonGrid grid;
    const auto hexAt = [&grid](int position) -> const Hex& {
        return grid.getHexByPosition(static_cast<uint32_t>(position))->get();
    };

    // A rectangle around a mid-map hex, in the grid's screen coordinates.
    const Hex& centre = hexAt(100 * HexagonGrid::GRID_WIDTH + 100);
    const ScreenRect rect{ static_cast<float>(centre.x()) - 160.f, static_cast<float>(centre.y()) - 90.f,
        static_cast<float>(centre.x()) + 160.f, static_cast<float>(centre.y()) + 90.f };

    const auto border = grid.screenRectangleBorderPositions(rect.left, rect.top, rect.right, rect.bottom);
    REQUIRE(border.size() > 20);
    REQUIRE(std::ranges::is_sorted(border));

    // Every ring hex sits inside the rectangle along one of its sides; walking hex rows from the
    // corners strays far from them instead.
    for (int position : border) {
        INFO("hex " << position);
        REQUIRE(rect.contains(hexAt(position)));
        CHECK(rect.hugsAnEdge(hexAt(position)));
    }

    // The ring is one hex-connected loop, so nothing inside can be reached without crossing it.
    const std::set<int> ring(border.begin(), border.end());
    CHECK(connectedWithin(ring, border.front()) == ring);

    // Every hex inside the rectangle but off the ring has all six neighbours inside it.
    for (const auto& hex : grid.grid()) {
        const auto position = static_cast<int>(hex.position());
        if (!rect.contains(hex) || ring.contains(position)) {
            continue;
        }
        for (int neighbour : hexgrid::neighbors(position)) {
            CHECK(rect.contains(hexAt(neighbour)));
        }
    }

    CHECK(grid.screenRectangleBorderPositions(-100.f, -100.f, -50.f, -50.f).empty());
}

TEST_CASE("HexagonGrid position<->coordinates round-trip over the whole grid", "[hexagon_grid]") {
    HexagonGrid grid;

    // Row-major mapping: position = y * GRID_WIDTH + x.
    const auto origin = grid.coordinatesForPosition(0);
    REQUIRE(origin.has_value());
    CHECK(origin->x == 0);
    CHECK(origin->y == 0);

    const auto oneOne = grid.coordinatesForPosition(HexagonGrid::GRID_WIDTH + 1);
    REQUIRE(oneOne.has_value());
    CHECK(oneOne->x == 1);
    CHECK(oneOne->y == 1);

    const auto last = grid.coordinatesForPosition(HexagonGrid::POSITION_COUNT - 1);
    REQUIRE(last.has_value());
    CHECK(last->x == HexagonGrid::GRID_WIDTH - 1);
    CHECK(last->y == HexagonGrid::GRID_HEIGHT - 1);

    // Every position converts to coordinates and back to the same position.
    for (int position = 0; position < HexagonGrid::POSITION_COUNT; ++position) {
        const auto coords = grid.coordinatesForPosition(position);
        REQUIRE(coords.has_value());
        const auto roundTripped = grid.positionForCoordinates(coords->x, coords->y);
        REQUIRE(roundTripped.has_value());
        REQUIRE(*roundTripped == position);
    }
}

TEST_CASE("HexagonGrid rejects out-of-range positions and coordinates", "[hexagon_grid]") {
    HexagonGrid grid;

    CHECK_FALSE(grid.coordinatesForPosition(-1).has_value());
    CHECK_FALSE(grid.coordinatesForPosition(HexagonGrid::POSITION_COUNT).has_value());
    CHECK_FALSE(grid.tileIndexForPosition(HexagonGrid::POSITION_COUNT).has_value());
    CHECK_FALSE(grid.getHexByPosition(HexagonGrid::POSITION_COUNT).has_value());

    CHECK_FALSE(grid.positionForCoordinates(-1, 0).has_value());
    CHECK_FALSE(grid.positionForCoordinates(HexagonGrid::GRID_WIDTH, 0).has_value());
    CHECK_FALSE(grid.positionForCoordinates(0, HexagonGrid::GRID_HEIGHT).has_value());
}

TEST_CASE("HexagonGrid positionAt resolves a screen point to the hex sitting there", "[hexagon_grid]") {
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
