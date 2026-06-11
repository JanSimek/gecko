#pragma once

#include "HexagonGrid.h"

#include <optional>

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

} // namespace geck::hexgrid
