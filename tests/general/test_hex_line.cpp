#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "editor/HexagonGrid.h"
#include "util/HexLine.h"

using geck::HexagonGrid;
namespace hexline = geck::hexline;

namespace {

// True if every consecutive pair in `line` is a hex-grid neighbour — i.e. the walk left no gaps.
bool isGapFree(const std::vector<int>& line) {
    for (std::size_t i = 1; i < line.size(); ++i) {
        const auto neighbours = hexline::hexNeighbors(line[i - 1]);
        if (std::find(neighbours.begin(), neighbours.end(), line[i]) == neighbours.end()) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("hexLine includes both endpoints", "[hexline]") {
    HexagonGrid grid;
    const int start = 100 * HexagonGrid::GRID_WIDTH + 80;
    const int end = 100 * HexagonGrid::GRID_WIDTH + 120;

    const auto line = hexline::hexLine(grid, start, end);
    REQUIRE(line.size() >= 2);
    CHECK(line.front() == start);
    CHECK(line.back() == end);
}

TEST_CASE("hexLine on a horizontal screen run is gap-free", "[hexline]") {
    HexagonGrid grid;
    // Two hexes on the same screen row, well apart horizontally.
    const int start = 100 * HexagonGrid::GRID_WIDTH + 60;
    const int end = 100 * HexagonGrid::GRID_WIDTH + 140;

    const auto line = hexline::hexLine(grid, start, end);
    REQUIRE(line.size() >= 2);
    CHECK(line.front() == start);
    CHECK(line.back() == end);
    CHECK(isGapFree(line));

    // Every hex on the line is distinct (a straight run never revisits a hex).
    auto sorted = line;
    std::ranges::sort(sorted);
    CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());
}

TEST_CASE("hexLine on a diagonal screen run is gap-free and reaches the end", "[hexline]") {
    HexagonGrid grid;
    // A diagonal: differ in both row and column so the iso staircase must step in both axes.
    const int start = 80 * HexagonGrid::GRID_WIDTH + 80;
    const int end = 120 * HexagonGrid::GRID_WIDTH + 120;

    const auto line = hexline::hexLine(grid, start, end);
    REQUIRE(line.size() >= 2);
    CHECK(line.front() == start);
    CHECK(line.back() == end);
    CHECK(isGapFree(line));
}

TEST_CASE("hexLine from a hex to itself is just that hex", "[hexline]") {
    HexagonGrid grid;
    const int hex = 50 * HexagonGrid::GRID_WIDTH + 50;
    const auto line = hexline::hexLine(grid, hex, hex);
    REQUIRE(line.size() == 1);
    CHECK(line.front() == hex);
}

TEST_CASE("hexLine with an off-grid endpoint is empty", "[hexline]") {
    HexagonGrid grid;
    const int valid = 100 * HexagonGrid::GRID_WIDTH + 100;
    CHECK(hexline::hexLine(grid, -1, valid).empty());
    CHECK(hexline::hexLine(grid, valid, HexagonGrid::POSITION_COUNT).empty());
}

TEST_CASE("hexline::isValidHex matches the grid bounds", "[hexline]") {
    CHECK(hexline::isValidHex(0));
    CHECK(hexline::isValidHex(HexagonGrid::POSITION_COUNT - 1));
    CHECK_FALSE(hexline::isValidHex(-1));
    CHECK_FALSE(hexline::isValidHex(HexagonGrid::POSITION_COUNT));
}
