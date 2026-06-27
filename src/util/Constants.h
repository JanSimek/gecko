#pragma once

#include <cstdint>

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
constexpr int ELEVATION_1 = 0;
constexpr int ELEVATION_2 = 1;
constexpr int ELEVATION_3 = 2;

// Tile positioning constants
constexpr int TILE_WIDTH = 80;          ///< Width of a tile in pixels
constexpr int TILE_HEIGHT = 36;         ///< Height of a tile in pixels
constexpr int TILE_X_OFFSET = 48;       ///< X offset for tile positioning
constexpr int TILE_Y_OFFSET_LARGE = 32; ///< Large Y offset for tile positioning
constexpr int TILE_Y_OFFSET_SMALL = 24; ///< Small Y offset for tile positioning
constexpr int TILE_Y_OFFSET_TINY = 12;  ///< Tiny Y offset for tile positioning

constexpr int ROOF_OFFSET = 96; ///< Roof height offset (adjusted for coordinate system alignment)

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
    // Unified selection accent — the theme PRIMARY (#4A90E2). Every selection visual (objects,
    // floor/roof tiles, hexes, the marquee) uses this one accent; the ERROR_* red below is
    // reserved strictly for errors/invalid states.
    constexpr int SELECTION_R = 74;
    constexpr int SELECTION_G = 144;
    constexpr int SELECTION_B = 226;

    // Selection rectangle (marquee) alphas (RGB comes from the accent above).
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

    // Tile selection overlay alpha (floor + roof) — higher visibility.
    constexpr int SELECTION_TILE_ALPHA = 220;

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

// Object rotation constants
namespace Rotation {
    constexpr int DEFAULT_DIRECTION = 0; ///< Default object direction
    constexpr int DIRECTION_RESET = 0;   ///< Reset direction value
}

// File format constants
namespace FileFormat {
    constexpr int TYPE_MASK_SHIFT = 24;              ///< Bit shift for type field in PIDs/FIDs
    constexpr uint32_t TYPE_MASK = 0x0F000000;       ///< FID type mask: high nibble only (bits 28-30 hold rotation)
    constexpr uint32_t FULL_TYPE_MASK = 0xFF000000;  ///< PID type mask: full high byte (PIDs carry no rotation)
    constexpr uint32_t BASE_ID_MASK = 0x00FFFFFF;    ///< Base ID mask for PIDs/FIDs
    constexpr uint32_t CRITTER_ID_MASK = 0x00000FFF; ///< Special mask for critter IDs
    constexpr int FALLOUT2_MAP_VERSION = 20;         ///< Standard Fallout 2 map version
}

// Wall Blocker constants
namespace WallBlockers {
    // Proto IDs for wall blockers (MISC type objects)
    // These objects mark hexes as unwalkable for pathfinding
    constexpr uint32_t NORMAL_WALL_BLOCKER_PID = 0x05000000 | 620;   ///< Proto 620 - Normal wall blocker
    constexpr uint32_t SHOOT_THROUGH_BLOCKER_PID = 0x05000000 | 621; ///< Proto 621 - Shoot-through wall blocker

    // Base ID for scroll blocker visualization (from FRM_PID)
    constexpr uint32_t SCROLL_BLOCKER_BASE_ID = 1; ///< Base ID for scroll blocker FRM

    // Flag bit for blocking objects
    constexpr uint32_t BLOCKING_FLAG = 0x00000010; ///< Flag indicating object blocks movement

    // MISC object type ID
    constexpr uint32_t MISC_TYPE_ID = 5; ///< MISC type in object type enum

    // Generic proto ID for simple objects
    constexpr uint32_t GENERIC_PROTO_ID = 24; ///< Proto 24 - Generic small object
}

// Exit Grid constants
//
// Exit-grid markers are MISC objects whose PROTO encodes the DIRECTION and whose FRM encodes the
// FAMILY (destination kind) + direction. The PROTO index runs 16..23; isExitGridMarker() requires
// exactly that range. Verified against the shipped game data (FRM dimensions via `frm info`) and
// bhrnddst.map (which uses protos 16-19):
//
//   dir  proto         greenFrm(=proto+1)   brownFrm(=proto+0x11)  size     drawn-line meaning
//   0    0x05000010    0x05000011 exitgrd1  0x05000021 ext2grd1    96x24    LEFT edge  (a vertical line)
//   1    0x05000011    0x05000012 exitgrd2  0x05000022 ext2grd2    96x24    RIGHT edge (a vertical line)
//   2    0x05000012    0x05000013 exitgrd3  0x05000023 ext2grd3    32x96    BOTTOM edge (a horizontal line)
//   3    0x05000013    0x05000014 exitgrd4  0x05000024 ext2grd4    32x96    TOP edge   (a horizontal line)
//   4    0x05000014    0x05000015 exitgrd5  0x05000025 ext2grd5    127x48   "/" diagonal, side A
//   5    0x05000015    0x05000016 exitgrd6  0x05000026 ext2grd6    127x48   "/" diagonal, side B
//   6    0x05000016    0x05000017 exitgrd7  0x05000027 ext2grd7    111x60   "\" diagonal, side A
//   7    0x05000017    0x05000018 exitgrd8  0x05000028 ext2grd8    111x60   "\" diagonal, side B
//
// Within a diagonal pair the two sides render near-identically (a sub-pixel facing offset), so the
// Marker-Direction override disambiguates when the auto pick is wrong.
//
// FAMILY = DESTINATION: an inter-map exit (exit_map is a real map id) uses the GREEN art (frm =
// proto + 1); a WORLD/TOWN exit (exit_map == WORLD_MAP_EXIT or TOWN_MAP_EXIT) uses the BROWN art
// (frm = proto + 0x11). The destination is stored per-object in the exit_* fields.
namespace ExitGrid {
    // Destination map IDs for special exits
    constexpr uint32_t TOWN_MAP_EXIT = 0xFFFFFFFF;  ///< -1: Opens town map view
    constexpr uint32_t WORLD_MAP_EXIT = 0xFFFFFFFE; ///< -2: Opens world map view

    // Proto PIDs for exit grids (MISC type 0x05, indices 16-23 = the eight directions)
    constexpr uint32_t FIRST_EXIT_GRID_PID = 0x05000010; ///< First exit grid proto (index 16, dir 0)
    constexpr uint32_t LAST_EXIT_GRID_PID = 0x05000017;  ///< Last exit grid proto (index 23, dir 7)

    // The eight directions, by the line a user draws / the iso edge the marker caps.
    enum Direction {
        DIR_LEFT = 0,   ///< vertical line, 96x24 wide bar
        DIR_RIGHT = 1,  ///< vertical line, 96x24 wide bar
        DIR_BOTTOM = 2, ///< horizontal line, 32x96 tall bar
        DIR_TOP = 3,    ///< horizontal line, 32x96 tall bar
        DIR_FWD_A = 4,  ///< "/" diagonal, 127x48, side A
        DIR_FWD_B = 5,  ///< "/" diagonal, 127x48, side B
        DIR_BACK_A = 6, ///< "\" diagonal, 111x60, side A
        DIR_BACK_B = 7, ///< "\" diagonal, 111x60, side B
        DIR_COUNT = 8
    };

    /// The MISC proto PID for a direction (index 16..23). isExitGridMarker() accepts exactly these.
    constexpr uint32_t exitGridProto(int dir) { return FIRST_EXIT_GRID_PID + static_cast<uint32_t>(dir); }
    /// The GREEN (inter-map) FRM PID for a direction's proto: frm = proto + 1.
    constexpr uint32_t greenFrm(int dir) { return exitGridProto(dir) + 1; }
    /// The BROWN (world/town-map) FRM PID for a direction's proto: frm = proto + 0x11.
    constexpr uint32_t brownFrm(int dir) { return exitGridProto(dir) + 0x11; }

    // Convenience aliases for the cardinal directions, used by the explicit Marker-Direction
    // override and the single-hex fallback. The matching FRM comes from greenFrm()/brownFrm() by
    // destination kind — these name only the proto (= direction).
    constexpr uint32_t LEFT_PRO_PID = exitGridProto(DIR_LEFT);     ///< 0x05000010, 96x24 (vertical line)
    constexpr uint32_t RIGHT_PRO_PID = exitGridProto(DIR_RIGHT);   ///< 0x05000011, 96x24 (vertical line)
    constexpr uint32_t BOTTOM_PRO_PID = exitGridProto(DIR_BOTTOM); ///< 0x05000012, 32x96 (horizontal line)
    constexpr uint32_t TOP_PRO_PID = exitGridProto(DIR_TOP);       ///< 0x05000013, 32x96 (horizontal line)
    constexpr uint32_t FWD_A_PRO_PID = exitGridProto(DIR_FWD_A);   ///< 0x05000014, 127x48 "/" side A
    constexpr uint32_t FWD_B_PRO_PID = exitGridProto(DIR_FWD_B);   ///< 0x05000015, 127x48 "/" side B
    constexpr uint32_t BACK_A_PRO_PID = exitGridProto(DIR_BACK_A); ///< 0x05000016, 111x60 "\" side A
    constexpr uint32_t BACK_B_PRO_PID = exitGridProto(DIR_BACK_B); ///< 0x05000017, 111x60 "\" side B
}

// Map defaults
namespace MapDefaults {
    constexpr uint32_t PLAYER_DEFAULT_POSITION = 19896; ///< Center of map (hex 99,99 area)
    constexpr uint32_t PLAYER_DEFAULT_ELEVATION = 0;    ///< Ground level
    constexpr uint32_t PLAYER_DEFAULT_ORIENTATION = 0;  ///< North
    constexpr int32_t NO_SCRIPT_ID = -1;                ///< No map script
    constexpr uint32_t DEFAULT_FLAGS = 0;               ///< All elevations enabled
    constexpr uint32_t NO_DARKNESS = 0;                 ///< No darkness
    constexpr uint32_t DEFAULT_MAP_ID = 0;              ///< Default map ID
    constexpr uint32_t DEFAULT_TIMESTAMP = 0;           ///< Default timestamp
}

// Sprite positioning constants
namespace SpriteOffset {
    constexpr int HEX_HIGHLIGHT_X = -16; ///< X offset for hex highlighting
    constexpr int HEX_HIGHLIGHT_Y = -8;  ///< Y offset for hex highlighting
}

// View constants - fixed size for consistent coordinate system
namespace View {
    constexpr float DEFAULT_WIDTH = 800.0f;  ///< Default view width
    constexpr float DEFAULT_HEIGHT = 600.0f; ///< Default view height
}

namespace UI {
    constexpr bool DEFAULT_SHOW_OBJECTS = true;
    constexpr bool DEFAULT_SHOW_CRITTERS = true;
    constexpr bool DEFAULT_SHOW_ROOF = true;
    constexpr bool DEFAULT_SHOW_WALLS = true;
    constexpr bool DEFAULT_SHOW_SCROLL_BLK = false;
    constexpr bool DEFAULT_SHOW_WALL_BLK = false;
    constexpr bool DEFAULT_SHOW_HEX_GRID = false;
}

// Additional color constants for player and UI elements
namespace PlayerColors {
    constexpr int POSITION_R = 50;      ///< Player position marker red
    constexpr int POSITION_G = 150;     ///< Player position marker green
    constexpr int POSITION_B = 255;     ///< Player position marker blue
    constexpr int POSITION_ALPHA = 200; ///< Player position marker alpha
}

namespace SelectionColors {
    // Hex selection uses the shared Colors::SELECTION_* accent; only the alpha is hex-specific.
    constexpr int HEX_ALPHA = 200; ///< Hex selection alpha

    constexpr int SCROLL_BLOCKER_R = 100;             ///< Scroll blocker rectangle red
    constexpr int SCROLL_BLOCKER_G = 255;             ///< Scroll blocker rectangle green
    constexpr int SCROLL_BLOCKER_B = 100;             ///< Scroll blocker rectangle blue
    constexpr int SCROLL_BLOCKER_FILL_ALPHA = 50;     ///< Scroll blocker fill alpha
    constexpr int SCROLL_BLOCKER_OUTLINE_ALPHA = 200; ///< Scroll blocker outline alpha
}

namespace OverlayColors {
    constexpr int WALL_BLOCKER_ALPHA = 180; ///< Wall blocker overlay alpha
}

} // namespace geck