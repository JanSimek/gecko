#pragma once

#include <cmath>
#include <cstdlib>
#include <vector>

#include "editor/Hex.h"
#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"

// A gap-free hex-line walk over the Fallout 2 iso hex grid, shared by the scripting host
// (MapScriptApi::placeExitGridRect) and the editor's exit-grid "Draw edge" tool so both
// trace an edge with one implementation. Qt-free: only depends on the hex grid geometry,
// so it is unit-testable headlessly.
namespace geck::hexline {

/// The direction index 0..5 of the single step from `fromHex` to the adjacent `toHex`, matching
/// hexgrid::DIRECTIONS / hexgrid::neighbors' order; -1 if either hex is off-grid or they are not immediate
/// neighbours. This is the parity-independent "which way did the chain turn" primitive: it is what
/// lets a wall-segment run condition each piece on the direction it enters and leaves a hex.
inline int hexDirection(int fromHex, int toHex) {
    using namespace geck::hexgrid;
    if (!isValidPosition(fromHex) || !isValidPosition(toHex)) {
        return -1;
    }
    const Cube delta = cubeOfPosition(toHex) - cubeOfPosition(fromHex);
    for (int i = 0; i < 6; ++i) {
        if (delta == DIRECTIONS[i]) {
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
/// Empty if either endpoint is off-grid.
inline std::vector<int> hexLine(int startHex, int endHex) {
    using namespace geck::hexgrid;
    std::vector<int> line;
    if (!isValidPosition(startHex) || !isValidPosition(endHex)) {
        return line;
    }

    const Cube a = cubeOfPosition(startHex);
    const Cube b = cubeOfPosition(endHex);
    const int n = distance(startHex, endHex);
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
        // A rounding step on a near-edge chord can land just off the grid: skip that cell rather than
        // push a bad hex that would poison downstream placement.
        if (const auto position = positionOf(cubeToOffset(c))) {
            line.push_back(*position);
        }
    }
    return line;
}

} // namespace geck::hexline
