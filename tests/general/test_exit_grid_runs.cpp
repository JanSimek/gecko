#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "editor/HexagonGrid.h"
#include "util/Constants.h"
#include "util/ExitGridRuns.h"
#include "util/HexLine.h"

using geck::HexagonGrid;
using geck::exitgrid_runs::analyse;
using geck::exitgrid_runs::isDiagonalDir;
using geck::exitgrid_runs::isHorizontalDir;
using geck::exitgrid_runs::Marker;
namespace ExitGrid = geck::ExitGrid;

namespace {
// Build markers along a real hex line so adjacency matches the renderer's hexNeighbors() grouping.
// `screenY` is faked monotonic so the run's "top" is deterministic (first hex = smallest Y).
std::vector<Marker> markersAlongLine(const HexagonGrid& grid, int aHex, int bHex, int dir) {
    std::vector<Marker> out;
    const auto hexes = geck::hexline::hexLine(grid, aHex, bHex);
    for (int y = 0; const int h : hexes) {
        out.push_back({ h, dir, 0, y++ });
    }
    return out;
}
} // namespace

TEST_CASE("isDiagonalDir / isHorizontalDir classify the eight directions", "[exitgrid][runs]") {
    CHECK_FALSE(isDiagonalDir(ExitGrid::DIR_LEFT));
    CHECK_FALSE(isDiagonalDir(ExitGrid::DIR_BOTTOM));
    CHECK(isDiagonalDir(ExitGrid::DIR_FWD_A));
    CHECK(isDiagonalDir(ExitGrid::DIR_BACK_B));
    CHECK(isHorizontalDir(ExitGrid::DIR_BOTTOM));
    CHECK(isHorizontalDir(ExitGrid::DIR_TOP));
    CHECK_FALSE(isHorizontalDir(ExitGrid::DIR_LEFT));
    CHECK_FALSE(isHorizontalDir(ExitGrid::DIR_FWD_A));
}

TEST_CASE("a diagonal run marks exactly one TOP (its smallest-screen-Y member)", "[exitgrid][runs]") {
    HexagonGrid grid;
    const int aHex = 95 * HexagonGrid::GRID_WIDTH + 100;  // (col 100, row 95)
    const int bHex = 108 * HexagonGrid::GRID_WIDTH + 100; // (col 100, row 108): a "\" run
    const auto markers = markersAlongLine(grid, aHex, bHex, ExitGrid::DIR_BACK_B);
    REQUIRE(markers.size() >= 3);

    const auto analysis = analyse(markers);
    int tops = 0;
    for (const auto& m : markers) {
        const auto it = analysis.byHex.find(m.hex);
        REQUIRE(it != analysis.byHex.end());
        if (it->second.isRunTop) {
            ++tops;
            CHECK(m.hex == markers.front().hex); // smallest faked screenY is the first hex
        }
    }
    CHECK(tops == 1);
}

TEST_CASE("two separate diagonal runs each get their own TOP", "[exitgrid][runs]") {
    HexagonGrid grid;
    auto runA = markersAlongLine(grid, 95 * HexagonGrid::GRID_WIDTH + 100,
        100 * HexagonGrid::GRID_WIDTH + 100, ExitGrid::DIR_BACK_B);
    // A far-away second run that cannot be hex-adjacent to the first.
    auto runB = markersAlongLine(grid, 95 * HexagonGrid::GRID_WIDTH + 140,
        100 * HexagonGrid::GRID_WIDTH + 140, ExitGrid::DIR_BACK_B);
    std::vector<Marker> all = runA;
    all.insert(all.end(), runB.begin(), runB.end());

    const auto analysis = analyse(all);
    int tops = 0;
    for (const auto& [hex, info] : analysis.byHex) {
        if (info.isRunTop) {
            ++tops;
        }
    }
    CHECK(tops == 2);
}

TEST_CASE("a diagonal end adjacent to a horizontal marker is a junction", "[exitgrid][runs]") {
    HexagonGrid grid;
    // A "\" run that bends into a screen-horizontal run sharing the bend hex's neighbourhood.
    const int bend = 104 * HexagonGrid::GRID_WIDTH + 100;
    auto diag = markersAlongLine(grid, 96 * HexagonGrid::GRID_WIDTH + 100, bend, ExitGrid::DIR_BACK_A);
    // Horizontal run from a hex adjacent to the bend (col -=2, row +=1 keeps it screen-horizontal).
    auto horiz = markersAlongLine(grid, bend, 112 * HexagonGrid::GRID_WIDTH + 84, ExitGrid::DIR_BOTTOM);
    // Drop the shared bend hex from the horizontal set so each hex has one direction.
    std::vector<Marker> all = diag;
    for (const auto& m : horiz) {
        if (m.hex != bend) {
            all.push_back(m);
        }
    }

    const auto analysis = analyse(all);
    REQUIRE_FALSE(analysis.junctions.empty());
    for (const auto& j : analysis.junctions) {
        // Each junction pairs a diagonal hex with an adjacent horizontal hex.
        const bool adjacent = [&] {
            for (const int nb : geck::hexline::hexNeighbors(j.diagonalHex)) {
                if (nb == j.horizontalHex) {
                    return true;
                }
            }
            return false;
        }();
        CHECK(adjacent);
    }
}

TEST_CASE("a pure cardinal placement yields no diagonal runs or junctions", "[exitgrid][runs]") {
    HexagonGrid grid;
    auto horiz = markersAlongLine(grid, 100 * HexagonGrid::GRID_WIDTH + 100,
        100 * HexagonGrid::GRID_WIDTH + 120, ExitGrid::DIR_BOTTOM);
    const auto analysis = analyse(horiz);
    CHECK(analysis.byHex.empty());     // no diagonal markers -> no per-marker directives
    CHECK(analysis.junctions.empty()); // junctions require a diagonal run
}
