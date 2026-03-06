# Fallout 2 Coordinate Systems Analysis

This document analyzes the coordinate systems used in various Fallout 2 map editors and engines, comparing them with our implementation.

## Overview

Fallout 2 uses a cavalier oblique projection (not true isometric) with two distinct coordinate systems:
- **Tile Grid**: 100×100 tiles per elevation (10,000 tiles total)
- **Hex Grid**: 200×200 hexes per elevation (40,000 hexes total)

## Coordinate Formulas Comparison

### 1. F2 Dims Mapper (Original)

The Dims mapper uses the following formulas for tile-to-screen conversion:

```
sx = 4752 + (32 * y) - (48 * x)
sy = (24 * y) + (12 * x)
```

Where:
- `x`, `y` are tile coordinates
- `sx`, `sy` are screen coordinates
- `4752` is a screen offset constant
- The magic numbers (48, 32, 24, 12) are derived from tile geometry

### 2. Reference Implementation (gecko-master)

```cpp
// From reference/gecko-master/src/state/EditorState.cpp
unsigned int tileX = static_cast<unsigned>(ceil(((double)tileNumber) / 100));
unsigned int tileY = tileNumber % 100;
unsigned int x = (100 - tileY - 1) * 48 + 32 * (tileX - 1);
unsigned int y = tileX * 24 + (tileY - 1) * 12 + 1;
```

Key differences:
- Uses 1-based indexing for `tileX` (ceil division)
- Subtracts 1 from both coordinates in the formula
- Adds `+ 1` to the final y coordinate

### 3. Fallout 2 Community Edition (fallout2-ce)

```cpp
// From fallout2-ce/src/tile.cc
int tileToScreenXY(int tile, int* screenX, int* screenY, int elevation) {
    int v3 = gHexGridWidth - 1 - tile % gHexGridWidth;
    int v4 = tile / gHexGridWidth;

    *screenX = _tile_offx;
    *screenY = _tile_offy;

    int v5 = (v3 - _tile_x) / -2;
    *screenX += 48 * ((v3 - _tile_x) / 2);
    *screenY += 12 * v5;

    // Handle odd tile adjustments
    if (v3 & 1) {
        if (v3 <= _tile_x) {
            *screenX -= 16;
            *screenY += 12;
        } else {
            *screenX += 32;
        }
    }

    int v6 = v4 - _tile_y;
    *screenX += 16 * v6;
    *screenY += 12 * v6;
}
```

Notable features:
- More complex calculation with viewport offsets
- Special handling for odd/even tiles
- Uses relative positioning from current tile position

### 4. Our Implementation

```cpp
// From src/util/TileUtils.h
unsigned int x = (MAP_WIDTH - coords.y - 1) * TILE_X_OFFSET + TILE_Y_OFFSET_LARGE * coords.x;
unsigned int y = coords.x * TILE_Y_OFFSET_SMALL + coords.y * TILE_Y_OFFSET_TINY + TILE_Y_OFFSET_TINY;
```

Where:
- `TILE_X_OFFSET = 48`
- `TILE_Y_OFFSET_LARGE = 32`
- `TILE_Y_OFFSET_SMALL = 24`
- `TILE_Y_OFFSET_TINY = 12`

## Key Findings

### 1. Magic Numbers Origin
The constants (48, 32, 24, 12) are derived from the tile geometry:
- **48**: Horizontal spacing between tiles in the same row
- **32**: Horizontal offset between rows
- **24**: Vertical spacing between rows
- **12**: Vertical offset for columns (and our correction factor)

### 2. The 12-Pixel Offset Issue
Our analysis revealed that tiles were misaligned with objects by approximately 12 pixels. This was resolved by adding `TILE_Y_OFFSET_TINY` (12) to the y-coordinate calculation, which aligns with:
- The reference implementation's approach (though they add only 1)
- The fundamental tile geometry where 12 pixels is the base vertical unit

### 3. Coordinate System Differences
Different implementations handle:
- **Index base**: Some use 0-based, others 1-based indexing
- **Viewport offsets**: Some include screen offset constants, others use relative positioning
- **Odd/even tile handling**: Some implementations have special cases for tile parity

### 4. Hex vs Tile Coordinates
Important distinction:
- **Tiles**: 100×100 grid, used for floor/roof graphics
- **Hexes**: 200×200 grid, used for objects and movement
- Each tile covers 4 hexes (2×2)

## Validation

Our current implementation correctly:
1. Uses the standard magic numbers (48, 32, 24, 12)
2. Applies the 12-pixel vertical offset for proper alignment
3. Maintains consistency between tile and object positioning
4. Follows the cavalier oblique projection used in Fallout 2

## Recommendations

1. **Keep the current formula**: Our implementation with the 12-pixel offset correctly aligns tiles and objects
2. **Document the constants**: The magic numbers should be well-documented as they represent fundamental tile geometry
3. **Test edge cases**: Ensure proper rendering at map boundaries and with odd/even tile positions
4. **Consider viewport offsets**: For future enhancements, we might need to add viewport-relative positioning like fallout2-ce

## References

- F2 Dims Mapper v0.99.3 by Dims (updated by Fakels/Mr.Stalin and Radegast)
- Fallout 2 Community Edition by alexbatalov: https://github.com/alexbatalov/fallout2-ce
- Fallout Modding Wiki: MAP File Format documentation
- Tim Cain's explanation of cavalier oblique projection in Fallout