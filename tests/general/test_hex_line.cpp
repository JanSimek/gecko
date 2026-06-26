#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "editor/Hex.h"
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

// The largest perpendicular screen distance of any line hex's centre from the straight chord
// between the two endpoint centres. A truly straight cube line hugs that chord; a greedy walk
// that minimises distance-to-endpoint bows far off it.
double maxOffsetFromChord(const HexagonGrid& grid, const std::vector<int>& line) {
    const auto first = grid.getHexByPosition(static_cast<uint32_t>(line.front()));
    const auto last = grid.getHexByPosition(static_cast<uint32_t>(line.back()));
    if (!first.has_value() || !last.has_value()) {
        return 0.0;
    }
    const double ax = first->get().x();
    const double ay = first->get().y();
    const double bx = last->get().x();
    const double by = last->get().y();
    const double len = std::hypot(bx - ax, by - ay);
    if (len == 0.0) {
        return 0.0;
    }
    double worst = 0.0;
    for (const int hex : line) {
        const auto h = grid.getHexByPosition(static_cast<uint32_t>(hex));
        if (!h.has_value()) {
            continue;
        }
        // Perpendicular distance from the point to the line through (ax,ay)-(bx,by).
        const double cross = std::abs((bx - ax) * (ay - h->get().y()) - (ax - h->get().x()) * (by - ay));
        worst = std::max(worst, cross / len);
    }
    return worst;
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

TEST_CASE("hexLine stays on the straight chord (no bowing)", "[hexline]") {
    HexagonGrid grid;
    // A spread of edges in different directions: horizontal, vertical, and two diagonals. The
    // old greedy distance-to-endpoint walk bowed hundreds of pixels off these; a cube line hugs
    // the chord to within about one hex.
    struct Edge {
        int start;
        int end;
    };
    const std::vector<Edge> edges = {
        { 100 * HexagonGrid::GRID_WIDTH + 40, 100 * HexagonGrid::GRID_WIDTH + 160 }, // wide horizontal
        { 40 * HexagonGrid::GRID_WIDTH + 100, 160 * HexagonGrid::GRID_WIDTH + 100 }, // tall vertical
        { 40 * HexagonGrid::GRID_WIDTH + 40, 160 * HexagonGrid::GRID_WIDTH + 160 },  // down-right diagonal
        { 40 * HexagonGrid::GRID_WIDTH + 160, 160 * HexagonGrid::GRID_WIDTH + 40 },  // down-left diagonal
    };

    for (const Edge& edge : edges) {
        const auto line = hexline::hexLine(grid, edge.start, edge.end);
        REQUIRE(line.size() >= 2);
        CHECK(line.front() == edge.start);
        CHECK(line.back() == edge.end);
        CHECK(isGapFree(line));

        // Every hex centre is within roughly one hex width of the chord — i.e. the line is straight.
        CHECK(maxOffsetFromChord(grid, line) <= static_cast<double>(geck::Hex::HEX_WIDTH));
    }
}
