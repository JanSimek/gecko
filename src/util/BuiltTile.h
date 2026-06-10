#pragma once

#include <cstdint>

namespace geck::built_tile {

// Engine "built tile" packing (fallout2-ce obj_types.h): the hex tile occupies
// the low bits and the elevation occupies bits 29-31. Used for scenery
// transition destinations and spatial-script positions.
inline constexpr uint32_t TILE_MASK = 0x03FFFFFF;
inline constexpr int ELEVATION_SHIFT = 29;
inline constexpr uint32_t ELEVATION_MASK = 0xE0000000;

constexpr uint32_t create(uint32_t tile, uint32_t elevation) {
    return (tile & TILE_MASK) | ((elevation << ELEVATION_SHIFT) & ELEVATION_MASK);
}

constexpr uint32_t tileOf(uint32_t builtTile) { return builtTile & TILE_MASK; }

constexpr uint32_t elevationOf(uint32_t builtTile) {
    return (builtTile & ELEVATION_MASK) >> ELEVATION_SHIFT;
}

} // namespace geck::built_tile
