#pragma once

namespace geck {

/**
 * @brief Common constants used throughout the Gecko application
 *
 * This file contains magic numbers that have been extracted into named constants
 * to improve code readability and maintainability.
 */

// Map and tile constants
constexpr int MAP_WIDTH = 100;             ///< Number of tiles per row in a map
constexpr int MAP_HEIGHT = 100;            ///< Number of tiles per column in a map
constexpr int TILES_PER_ELEVATION = 10000; ///< Total tiles per elevation (100x100)
constexpr int INVALID_ELEVATION = -1;      ///< Invalid or unset elevation value

// Tile positioning constants
constexpr int TILE_WIDTH = 80;          ///< Width of a tile in pixels
constexpr int TILE_HEIGHT = 36;         ///< Height of a tile in pixels
constexpr int TILE_X_OFFSET = 48;       ///< X offset for tile positioning
constexpr int TILE_Y_OFFSET_LARGE = 32; ///< Large Y offset for tile positioning
constexpr int TILE_Y_OFFSET_SMALL = 24; ///< Small Y offset for tile positioning
constexpr int TILE_Y_OFFSET_TINY = 12;  ///< Tiny Y offset for tile positioning
constexpr int ROOF_OFFSET = 96;         ///< Roof height offset

// View movement constants
constexpr float VIEW_MOVE_STEP = 50.0f; ///< Distance to move view with arrow keys

// Mouse and interaction constants
constexpr float TILE_CLICK_DISTANCE_THRESHOLD = 40.0f; ///< Maximum distance for tile click detection
constexpr float DRAG_START_THRESHOLD = 5.0f;           ///< Minimum drag distance to start drag operation

// Color constants
constexpr int HEX_HIGHLIGHT_ALPHA = 200; ///< Alpha value for hex highlighting

// Area selection padding constants
constexpr float AREA_SELECTION_X_PADDING = 40.0f;       ///< X padding for area selection bounds
constexpr float AREA_SELECTION_Y_PADDING = 18.0f;       ///< Y padding for area selection bounds
constexpr float AREA_SELECTION_X_TOTAL_PADDING = 80.0f; ///< Total X padding (left + right)
constexpr float AREA_SELECTION_Y_TOTAL_PADDING = 36.0f; ///< Total Y padding (top + bottom)

// Color constants (RGBA values)
namespace Colors {
    // Selection rectangle colors
    constexpr int SELECTION_RECT_R = 100;
    constexpr int SELECTION_RECT_G = 150;
    constexpr int SELECTION_RECT_B = 255;
    constexpr int SELECTION_RECT_FILL_ALPHA = 50;
    constexpr int SELECTION_RECT_OUTLINE_ALPHA = 200;

    // Preview highlight colors (yellow)
    constexpr int PREVIEW_R = 255;
    constexpr int PREVIEW_G = 255;
    constexpr int PREVIEW_B = 0;
    constexpr int PREVIEW_ALPHA = 150;
    constexpr int PREVIEW_OUTLINE_ALPHA = 255;

    // Error indicator colors (red)
    constexpr int ERROR_R = 255;
    constexpr int ERROR_G = 0;
    constexpr int ERROR_B = 0;
    constexpr int ERROR_ALPHA = 150;
    constexpr int ERROR_OUTLINE_ALPHA = 255;

    // Roof tile selection colors (higher visibility)
    constexpr int ROOF_SELECTION_ALPHA = 220;

    // Standard alpha values
    constexpr int FULLY_OPAQUE = 255;
    constexpr int SEMI_TRANSPARENT = 150;
}

// UI constants
namespace UI {
    constexpr int TITLE_FONT_SIZE = 24;   ///< Font size for titles
    constexpr int STATUS_FONT_SIZE = 14;  ///< Font size for status text
    constexpr int PROGRESS_BAR_MAX = 100; ///< Maximum value for progress bars
    constexpr int PROGRESS_BAR_MIN = 0;   ///< Minimum value for progress bars
    constexpr int TIMER_INTERVAL_MS = 33; ///< Timer interval for 30 FPS updates (33ms = ~30 FPS)
    constexpr int SPACING_SMALL = 10;     ///< Small spacing between widgets
    constexpr int SPACING_LARGE = 20;     ///< Large spacing between widgets
}

// Hexagon grid constants
namespace HexGrid {
    constexpr int HEX_WIDTH = 32;    ///< Width of a hexagon
    constexpr int HEX_HEIGHT = 16;   ///< Height of a hexagon
    constexpr int GRID_WIDTH = 200;  ///< Width of the hex grid
    constexpr int GRID_HEIGHT = 200; ///< Height of the hex grid
}

// Object rotation constants
namespace Rotation {
    constexpr int DEFAULT_DIRECTION = 0; ///< Default object direction
    constexpr int DIRECTION_RESET = 0;   ///< Reset direction value
}

// File format constants
namespace FileFormat {
    constexpr int TYPE_MASK_SHIFT = 24;             ///< Bit shift for type mask in PIDs/FIDs
    constexpr uint32_t TYPE_MASK = 0x0F000000;      ///< Type mask for PIDs/FIDs
    constexpr uint32_t FULL_TYPE_MASK = 0xFF000000; ///< Full type mask
}

} // namespace geck