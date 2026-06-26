#pragma once

#include <array>
#include <limits>
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

/// The up-to-six on-grid hex neighbours of `hex` (cube-coordinate, so parity-correct
/// regardless of the hex's column parity). Empty if `hex` is off-grid.
inline std::vector<int> hexNeighbors(int hex) {
    using namespace geck::hexgrid;
    // The six cube-coordinate neighbour directions.
    static constexpr std::array<Cube, 6> kDirs = {
        { { 1, -1, 0 }, { 1, 0, -1 }, { 0, 1, -1 }, { -1, 1, 0 }, { -1, 0, 1 }, { 0, -1, 1 } }
    };
    std::vector<int> result;
    if (!isValidHex(hex)) {
        return result;
    }
    const Cube c = cubeOfPosition(hex);
    for (const Cube& d : kDirs) {
        const ColRow cr = cubeToOffset(c + d);
        if (cr.col >= 0 && cr.col < WIDTH && cr.row >= 0 && cr.row < HEIGHT) {
            result.push_back(cr.row * WIDTH + cr.col);
        }
    }
    return result;
}

/// Squared screen distance from `hex` to the point (ex, ey); max if `hex` is off-grid.
inline long screenDistSq(const HexagonGrid& grid, int hex, int ex, int ey) {
    const auto h = grid.getHexByPosition(static_cast<uint32_t>(hex));
    if (!h.has_value()) {
        return std::numeric_limits<long>::max();
    }
    const long dx = h->get().x() - ex;
    const long dy = h->get().y() - ey;
    return dx * dx + dy * dy;
}

/// A gap-free run of hexes from `startHex` to `endHex`: greedily steps to the neighbour
/// whose screen position is nearest the end, so consecutive hexes are always screen-adjacent
/// (the iso staircase a straight screen-line walk would skip). Endpoints are included. Empty
/// if either endpoint is off-grid.
inline std::vector<int> hexLine(const HexagonGrid& grid, int startHex, int endHex) {
    std::vector<int> line;
    if (!isValidHex(startHex) || !isValidHex(endHex)) {
        return line;
    }
    const auto endRef = grid.getHexByPosition(static_cast<uint32_t>(endHex));
    if (!endRef.has_value()) {
        return line;
    }
    const int ex = endRef->get().x();
    const int ey = endRef->get().y();

    int cur = startHex;
    line.push_back(cur);
    // A straight screen edge needs at most a grid span's worth of steps; the bound also stops
    // a pathological non-converging walk.
    const int maxSteps = 2 * (HexagonGrid::GRID_WIDTH + HexagonGrid::GRID_HEIGHT);
    for (int step = 0; step < maxSteps && cur != endHex; ++step) {
        long best = screenDistSq(grid, cur, ex, ey);
        int next = -1;
        for (const int neighbour : hexNeighbors(cur)) {
            if (const long d = screenDistSq(grid, neighbour, ex, ey); d < best) {
                best = d;
                next = neighbour;
            }
        }
        if (next < 0) {
            break; // no neighbour is closer to the end — straight edges never hit this
        }
        cur = next;
        line.push_back(cur);
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
