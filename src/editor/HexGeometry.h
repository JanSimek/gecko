#pragma once

#include "HexagonGrid.h"

#include <optional>

// Fallout 2 hex-grid geometry: cube coordinates and 60-degree rotation.
//
// Positions are numbered row-major, position = row * WIDTH + col, over a WIDTH x HEIGHT
// grid (matches HexagonGrid). The grid uses a column-offset ("even-q") layout, so
// neighbour deltas in position space are parity-dependent (fallout2-ce src/tile.cc,
// `_dir_tile[parity][direction]`). Converting to cube coordinates removes that parity
// dependence and turns rotation into a simple, exact integer operation.
//
// Because the layout is offset-based, rotating a *relative* offset is only well-defined
// once it is resolved against an absolute pivot — hence the primitives here work on
// absolute positions (0 .. WIDTH*HEIGHT-1), not bare (dx, dy) deltas.
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
// exact even for negative col, making the conversion invertible over all integers (the
// rotated intermediate values routinely fall outside [0, WIDTH/HEIGHT)).
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

// Rotate a cube vector by `steps` 60-degree increments. One +step advances the Fallout 2
// direction index by one (validated against `_dir_tile` in the tests); +3 is a 180-degree
// point reflection and +6 is the identity. Negative steps rotate the other way.
constexpr Cube rotate(const Cube& c, int steps) {
    int s = steps % 6;
    if (s < 0) {
        s += 6;
    }
    Cube r = c;
    for (int i = 0; i < s; ++i) {
        r = { -r.y, -r.z, -r.x };
    }
    return r;
}

// Unit cube vector pointing along Fallout 2 hex direction d (0..5).
constexpr Cube directionCube(int d) { return rotate(Cube{ -1, 1, 0 }, d); }

// `direction` rotated by `steps`, normalised to 0..5 (mirrors the object-facing rotation
// `(direction + steps) % 6`).
constexpr int rotateDirection(int direction, int steps) {
    int d = (direction + steps) % 6;
    if (d < 0) {
        d += 6;
    }
    return d;
}

// Map `position` — authored relative to `fromAnchor` — to where it lands when the pattern
// is stamped at `toAnchor` rotated by `steps`. Returns std::nullopt if the result falls
// outside the grid. With fromAnchor == toAnchor this is an in-place rotation about a pivot.
inline std::optional<int> stamp(int position, int fromAnchor, int toAnchor, int steps) {
    const Cube rel = cubeOfPosition(position) - cubeOfPosition(fromAnchor);
    const ColRow placed = cubeToOffset(cubeOfPosition(toAnchor) + rotate(rel, steps));
    if (placed.col < 0 || placed.col >= WIDTH || placed.row < 0 || placed.row >= HEIGHT) {
        return std::nullopt;
    }
    return placed.row * WIDTH + placed.col;
}

// Rotate `position` by `steps` 60-degree increments about `pivot`.
inline std::optional<int> rotateAround(int position, int pivot, int steps) {
    return stamp(position, pivot, pivot, steps);
}

} // namespace geck::hexgrid
