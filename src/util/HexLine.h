#pragma once

#include <array>
#include <cmath>
#include <cstdlib>
#include <set>
#include <vector>

#include "editor/Hex.h"
#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"

// A gap-free hex-line walk over the Fallout 2 iso hex grid, shared by the scripting host
// (MapScriptApi::placeExitGridRect) and the editor's exit-grid "Draw edge" tool so both
// trace an edge with one implementation. Qt-free: only depends on the hex grid geometry,
// so it is unit-testable headlessly.
namespace geck::hexline {

/// On the 200x200 hex grid: 0 <= hex < POSITION_COUNT.
inline bool isValidHex(int hex) {
    return hex >= 0 && hex < HexagonGrid::POSITION_COUNT;
}

/// The six cube-coordinate neighbour directions, in the canonical order that indexes a
/// direction 0..5. hexNeighbors() walks them in this order (skipping off-grid cells) and
/// hexDirection() reports a step's index against it, so the two agree on what "direction i" means.
inline constexpr std::array<geck::hexgrid::Cube, 6> kHexDirs = {
    { { 1, -1, 0 }, { 1, 0, -1 }, { 0, 1, -1 }, { -1, 1, 0 }, { -1, 0, 1 }, { 0, -1, 1 } }
};

/// The up-to-six on-grid hex neighbours of `hex` (cube-coordinate, so parity-correct
/// regardless of the hex's column parity). Empty if `hex` is off-grid.
inline std::vector<int> hexNeighbors(int hex) {
    using namespace geck::hexgrid;
    std::vector<int> result;
    if (!isValidHex(hex)) {
        return result;
    }
    const Cube c = cubeOfPosition(hex);
    for (const Cube& d : kHexDirs) {
        if (const ColRow cr = cubeToOffset(c + d);
            cr.col >= 0 && cr.col < WIDTH && cr.row >= 0 && cr.row < HEIGHT) {
            result.push_back(cr.row * WIDTH + cr.col);
        }
    }
    return result;
}

/// The direction index 0..5 of the single step from `fromHex` to the adjacent `toHex`, matching
/// kHexDirs / hexNeighbors' order; -1 if either hex is off-grid or they are not immediate
/// neighbours. This is the parity-independent "which way did the chain turn" primitive: it is what
/// lets a wall-segment run condition each piece on the direction it enters and leaves a hex.
inline int hexDirection(int fromHex, int toHex) {
    using namespace geck::hexgrid;
    if (!isValidHex(fromHex) || !isValidHex(toHex)) {
        return -1;
    }
    const Cube delta = cubeOfPosition(toHex) - cubeOfPosition(fromHex);
    for (int i = 0; i < 6; ++i) {
        if (delta == kHexDirs[i]) {
            return i;
        }
    }
    return -1;
}

namespace detail {

    /// Linear interpolation of a single cube axis between `a` and `b` at parameter `t` in [0, 1].
    inline double cubeLerp(int a, int b, double t) {
        return static_cast<double>(a) + static_cast<double>(b - a) * t;
    }

    /// Round a fractional cube (x, y, z) to the nearest valid integer cube (x + y + z == 0):
    /// round each axis, then nudge the component with the largest rounding error so the sum stays 0.
    inline geck::hexgrid::Cube cubeRound(double fx, double fy, double fz) {
        long rx = std::lround(fx);
        long ry = std::lround(fy);
        long rz = std::lround(fz);

        const double dx = std::abs(static_cast<double>(rx) - fx);
        const double dy = std::abs(static_cast<double>(ry) - fy);
        const double dz = std::abs(static_cast<double>(rz) - fz);

        if (dx > dy && dx > dz) {
            rx = -ry - rz;
        } else if (dy > dz) {
            ry = -rx - rz;
        } else {
            rz = -rx - ry;
        }
        return { static_cast<int>(rx), static_cast<int>(ry), static_cast<int>(rz) };
    }

} // namespace detail

/// A gap-free run of hexes from `startHex` to `endHex` along the straight cube-coordinate chord:
/// the endpoints are exact, consecutive results are grid neighbours (no gaps), and no hex repeats.
/// `grid` is unused — the walk is pure grid geometry — but the signature is kept so callers
/// (ExitGridPlacementManager, MapScriptApi::placeExitGridRect) don't churn. Empty if either
/// endpoint is off-grid.
inline std::vector<int> hexLine([[maybe_unused]] const HexagonGrid& grid, int startHex, int endHex) {
    using namespace geck::hexgrid;
    std::vector<int> line;
    if (!isValidHex(startHex) || !isValidHex(endHex)) {
        return line;
    }

    const Cube a = cubeOfPosition(startHex);
    const Cube b = cubeOfPosition(endHex);
    // Cube distance = the number of single-hex steps between the endpoints.
    const int n = (std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z)) / 2;
    if (n == 0) {
        line.push_back(startHex); // start == end: a single hex.
        return line;
    }

    line.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n);
        const Cube c = detail::cubeRound(
            detail::cubeLerp(a.x, b.x, t),
            detail::cubeLerp(a.y, b.y, t),
            detail::cubeLerp(a.z, b.z, t));
        const ColRow cr = cubeToOffset(c);
        // Bounds-check the rounded offset before indexing — a rounding step on a near-edge chord can
        // land just off the grid, and an off-grid index would poison downstream placement. Mirror the
        // validity check in hexNeighbors(): skip any out-of-range cell rather than push a bad hex.
        if (cr.col < 0 || cr.col >= WIDTH || cr.row < 0 || cr.row >= HEIGHT) {
            continue;
        }
        line.push_back(cr.row * WIDTH + cr.col);
    }
    return line;
}

/// The deduped, gap-free hex run through a polyline of hex indices: consecutive vertices are joined
/// by hexLine() and the result has each hex once, in first-seen order. Off-grid vertices (hex < 0)
/// are skipped. Both the editor's "Draw edge" tool and its preview use this so they trace the same
/// hexes from the same vertex list.
inline std::vector<int> hexPolyline(const HexagonGrid& grid, const std::vector<int>& vertexHexes) {
    std::vector<int> result;
    std::set<int> seen;
    int prevHex = -1;
    for (const int hex : vertexHexes) {
        if (hex < 0) {
            continue;
        }
        const std::vector<int> segment = (prevHex >= 0)
            ? hexLine(grid, prevHex, hex)
            : std::vector<int>{ hex };
        for (const int lineHex : segment) {
            if (seen.insert(lineHex).second) {
                result.push_back(lineHex);
            }
        }
        prevHex = hex;
    }
    return result;
}

} // namespace geck::hexline
