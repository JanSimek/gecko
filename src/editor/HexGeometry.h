#pragma once

#include "HexagonGrid.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

// Fallout 2 hex-grid geometry: cube coordinates and shape-preserving translation.
//
// Positions are numbered row-major, position = row * WIDTH + col, over a WIDTH x HEIGHT
// grid (matches HexagonGrid). The grid uses a column-offset ("even-q") layout, so
// neighbour deltas in position space are parity-dependent (fallout2-ce src/tile.cc,
// `_dir_tile[parity][direction]`). Converting to cube coordinates removes that parity
// dependence, so translating a captured shape from one anchor to another (the stamp
// operation) preserves its layout even when the anchors have different column parity.
namespace geck::hexgrid {

inline constexpr int WIDTH = HexagonGrid::GRID_WIDTH;
inline constexpr int HEIGHT = HexagonGrid::GRID_HEIGHT;

struct Cube {
    int x = 0;
    int y = 0;
    int z = 0;
    constexpr bool operator==(const Cube&) const = default;
};

constexpr Cube operator+(const Cube& a, const Cube& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
constexpr Cube operator-(const Cube& a, const Cube& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

constexpr int columnOf(int position) { return position % WIDTH; }
constexpr int rowOf(int position) { return position / WIDTH; }

struct ColRow {
    int col = 0;
    int row = 0;
    constexpr bool operator==(const ColRow&) const = default;
};

// even-q offset (col, row) -> cube. `col + (col & 1)` is always even, so the division is
// exact even for negative col, making the conversion invertible over all integers (a
// translated intermediate value can fall outside [0, WIDTH/HEIGHT)).
constexpr Cube offsetToCube(int col, int row) {
    const int x = col;
    const int z = row - (col + (col & 1)) / 2;
    return { x, -x - z, z };
}

constexpr ColRow cubeToOffset(const Cube& c) {
    return { c.x, c.z + (c.x + (c.x & 1)) / 2 };
}

constexpr Cube cubeOfPosition(int position) {
    return offsetToCube(columnOf(position), rowOf(position));
}

// Map `position` — authored relative to `fromAnchor` — to where it lands when the shape
// is stamped at `toAnchor`. Translation happens in cube space so the shape is preserved
// regardless of the two anchors' column parity. Returns std::nullopt if the result falls
// outside the grid.
inline std::optional<int> translate(int position, int fromAnchor, int toAnchor) {
    const Cube placed = cubeOfPosition(toAnchor) + (cubeOfPosition(position) - cubeOfPosition(fromAnchor));
    const ColRow cr = cubeToOffset(placed);
    if (cr.col < 0 || cr.col >= WIDTH || cr.row < 0 || cr.row >= HEIGHT) {
        return std::nullopt;
    }
    return cr.row * WIDTH + cr.col;
}

// A hex within a disc, paired with its hex-step distance from the centre.
struct HexAtDistance {
    int position = 0;
    int distance = 0;
};

// A filled hex disc of hex-distance `radius` around `centerPosition`, each hex paired with its
// hex-step distance from the centre (0 at the centre, up to `radius`), in ascending position order
// per the row-major cube sweep. Cube distance (|dx|+|dy|+|dz|)/2 equals the engine's hex-step
// distance (fallout2-ce `tileDistanceBetween`) because the offset<->cube conversion mirrors the
// engine's `_dir_tile` parity layout. Off-grid hexes are skipped, so a disc near an edge is clipped;
// `radius` < 0 yields an empty list. This is the primitive behind hexesWithinRadius (which drops the
// distance) and the per-ring light-overlay falloff.
inline std::vector<HexAtDistance> hexDiscByDistance(int centerPosition, int radius) {
    std::vector<HexAtDistance> hexes;
    if (radius < 0) {
        return hexes;
    }
    const Cube center = cubeOfPosition(centerPosition);
    for (int dx = -radius; dx <= radius; ++dx) {
        const int lo = std::max(-radius, -dx - radius);
        const int hi = std::min(radius, -dx + radius);
        for (int dy = lo; dy <= hi; ++dy) {
            const int dz = -dx - dy;
            const ColRow cr = cubeToOffset(center + Cube{ dx, dy, dz });
            if (cr.col < 0 || cr.col >= WIDTH || cr.row < 0 || cr.row >= HEIGHT) {
                continue;
            }
            const int distance = (std::abs(dx) + std::abs(dy) + std::abs(dz)) / 2;
            hexes.push_back({ cr.row * WIDTH + cr.col, distance });
        }
    }
    return hexes;
}

// All grid positions within hex-distance `radius` of `centerPosition` — the same filled disc as
// hexDiscByDistance with the per-hex distance dropped. Matches the engine's spatial-script trigger
// test `tileDistanceBetween(centre, h) <= radius`.
inline std::vector<int> hexesWithinRadius(int centerPosition, int radius) {
    const auto disc = hexDiscByDistance(centerPosition, radius);
    std::vector<int> positions;
    positions.reserve(disc.size());
    for (const auto& hex : disc) {
        positions.push_back(hex.position);
    }
    return positions;
}

} // namespace geck::hexgrid
