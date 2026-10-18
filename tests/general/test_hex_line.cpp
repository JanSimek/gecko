#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "editor/Hex.h"
#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "util/HexLine.h"

using geck::HexagonGrid;
namespace hexline = geck::hexline;

namespace {

// True if every consecutive pair in `line` is a hex-grid neighbour — i.e. the walk left no gaps.
bool isGapFree(const std::vector<int>& line) {
    for (std::size_t i = 1; i < line.size(); ++i) {
        const auto neighbours = geck::hexgrid::neighbors(line[i - 1]);
        if (std::ranges::find(neighbours, line[i]) == neighbours.end()) {
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

TEST_CASE("hexLine is a straight, gap-free walk between its endpoints", "[hexline]") {
    constexpr int W = HexagonGrid::GRID_WIDTH;
    // Edges in different directions: horizontal, vertical, and both diagonals, so the iso staircase
    // must step in both axes. The old greedy distance-to-endpoint walk bowed hundreds of pixels off
    // the long ones; a cube line hugs the chord to within about one hex.
    const auto [start, end] = GENERATE(table<int, int>({
        { 100 * W + 80, 100 * W + 120 }, // short horizontal
        { 100 * W + 60, 100 * W + 140 }, // horizontal
        { 80 * W + 80, 120 * W + 120 },  // diagonal
        { 100 * W + 40, 100 * W + 160 }, // wide horizontal
        { 40 * W + 100, 160 * W + 100 }, // tall vertical
        { 40 * W + 40, 160 * W + 160 },  // down-right diagonal
        { 40 * W + 160, 160 * W + 40 },  // down-left diagonal
    }));
    INFO("start " << start << " end " << end);

    const auto line = hexline::hexLine(start, end);
    REQUIRE(line.size() >= 2);
    CHECK(line.front() == start);
    CHECK(line.back() == end);
    CHECK(isGapFree(line));

    // Every hex on the line is distinct (a straight run never revisits a hex).
    auto sorted = line;
    std::ranges::sort(sorted);
    CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());

    // Every hex centre is within roughly one hex width of the chord, i.e. the line is straight.
    const HexagonGrid grid;
    CHECK(maxOffsetFromChord(grid, line) <= static_cast<double>(geck::Hex::HEX_WIDTH));
}

TEST_CASE("hexLine from a hex to itself is just that hex", "[hexline]") {
    const int hex = 50 * HexagonGrid::GRID_WIDTH + 50;
    const auto line = hexline::hexLine(hex, hex);
    REQUIRE(line.size() == 1);
    CHECK(line.front() == hex);
}

TEST_CASE("hexLine with an off-grid endpoint is empty", "[hexline]") {
    const int valid = 100 * HexagonGrid::GRID_WIDTH + 100;
    CHECK(hexline::hexLine(-1, valid).empty());
    CHECK(hexline::hexLine(valid, HexagonGrid::POSITION_COUNT).empty());
}

TEST_CASE("hexDirection inverts hexgrid::neighbors' order", "[hexline]") {
    // For an interior hex every neighbour is on-grid, so neighbors() returns all six in DIRECTIONS
    // order and the i-th one must report direction i. This is the contract the wall-segment chain
    // relies on: the step onto a neighbour has a stable, parity-independent direction index.
    const int hex = 100 * HexagonGrid::GRID_WIDTH + 100;
    const auto neighbours = geck::hexgrid::neighbors(hex);
    REQUIRE(neighbours.size() == 6);
    for (int dir = 0; dir < 6; ++dir) {
        CHECK(hexline::hexDirection(hex, neighbours[dir]) == dir);
    }
}

TEST_CASE("hexDirection is antisymmetric and parity-independent", "[hexline]") {
    // The reverse step is the opposite direction (d and d+3 are the three opposite pairs), and this
    // holds regardless of the hexes' column parity — the reason wall pieces on either side of a
    // shared edge attach: hex A's direction d onto B is B's direction d+3 back onto A.
    for (const int col : { 40, 41 }) { // one even, one odd column
        const int hex = 100 * HexagonGrid::GRID_WIDTH + col;
        for (const int n : geck::hexgrid::neighbors(hex)) {
            const int forward = hexline::hexDirection(hex, n);
            const int back = hexline::hexDirection(n, hex);
            REQUIRE(forward >= 0);
            REQUIRE(back >= 0);
            CHECK((forward + 3) % 6 == back);
        }
    }
}

TEST_CASE("hexDirection is -1 for non-neighbours and off-grid hexes", "[hexline]") {
    const int hex = 100 * HexagonGrid::GRID_WIDTH + 100;
    CHECK(hexline::hexDirection(hex, hex) == -1);                         // a hex is not its own neighbour
    CHECK(hexline::hexDirection(hex, hex + 2) == -1);                     // two columns away: not adjacent
    CHECK(hexline::hexDirection(hex, -1) == -1);                          // off-grid target
    CHECK(hexline::hexDirection(-1, hex) == -1);                          // off-grid source
    CHECK(hexline::hexDirection(hex, HexagonGrid::POSITION_COUNT) == -1); // off-grid target (upper bound)
}
