#pragma once

#include <string_view>

namespace geck {

/**
 * @brief Centralized resource paths for all FRM, LST, and other game files
 * 
 * This file contains all hardcoded paths to game resources to prevent
 * path errors and improve maintainability. All paths are relative to
 * the game data directory.
 */
namespace ResourcePaths {

    // ========================================
    // LST Files - File listings
    // ========================================
    namespace Lst {
        constexpr std::string_view ITEMS      = "art/items/items.lst";
        constexpr std::string_view CRITTERS   = "art/critters/critters.lst";
        constexpr std::string_view SCENERY    = "art/scenery/scenery.lst";
        constexpr std::string_view WALLS      = "art/walls/walls.lst";
        constexpr std::string_view TILES      = "art/tiles/tiles.lst";
        constexpr std::string_view MISC       = "art/misc/misc.lst";
        constexpr std::string_view INTERFACE  = "art/intrface/intrface.lst";
        constexpr std::string_view INVENTORY  = "art/inven/inven.lst";
        
        // Proto lists
        constexpr std::string_view PROTO_ITEMS    = "proto/items/items.lst";
        constexpr std::string_view PROTO_CRITTERS = "proto/critters/critters.lst";
        constexpr std::string_view PROTO_SCENERY  = "proto/scenery/scenery.lst";
        constexpr std::string_view PROTO_WALLS    = "proto/walls/walls.lst";
        constexpr std::string_view PROTO_TILES    = "proto/tiles/tiles.lst";
        constexpr std::string_view PROTO_MISC     = "proto/misc/misc.lst";
        
        // Scripts
        constexpr std::string_view SCRIPTS = "scripts/scripts.lst";
    }

    // ========================================
    // FRM Files - Sprites and graphics
    // ========================================
    namespace Frm {
        // Special FRM files
        constexpr std::string_view HEX_GRID       = "art/misc/HEX.frm";
        constexpr std::string_view SCROLL_BLOCKER  = "art/misc/scrblk.frm";
        constexpr std::string_view WALL_BLOCK      = "art/misc/wallblock.frm";
        constexpr std::string_view WALL_BLOCK_FULL = "art/misc/wallblockF.frm";
        
        // Light source (special case)
        constexpr std::string_view LIGHT = "art/misc/light.frm";
        
        // Exit grid marker
        constexpr std::string_view EXIT_GRID = "art/misc/exitgrid.frm";
        
        // Blank tile texture
        constexpr std::string_view BLANK_TILE = "art/tiles/blank.frm";
        
        // Standing animation suffix for critters
        constexpr std::string_view STANDING_SUFFIX = "aa.frm";
    }

    // ========================================
    // PAL Files - Color palettes
    // ========================================
    namespace Pal {
        constexpr std::string_view COLOR = "color.pal";
    }

    // ========================================
    // MSG Files - Text messages
    // ========================================
    namespace Msg {
        constexpr std::string_view PRO_ITEM = "text/english/game/pro_item.msg";
        constexpr std::string_view PRO_CRIT = "text/english/game/pro_crit.msg";
        constexpr std::string_view PRO_SCEN = "text/english/game/pro_scen.msg";
        constexpr std::string_view PRO_WALL = "text/english/game/pro_wall.msg";
        constexpr std::string_view PRO_TILE = "text/english/game/pro_tile.msg";
        constexpr std::string_view PRO_MISC = "text/english/game/pro_misc.msg";
        
        // Game data messages
        constexpr std::string_view STAT = "text/english/game/stat.msg";
        constexpr std::string_view PERK = "text/english/game/perk.msg";
        
        // Script names
        constexpr std::string_view SCRNAME = "scrname.msg";
    }

    // ========================================
    // Directory prefixes for FRM types
    // ========================================
    namespace Directories {
        constexpr std::string_view ITEMS     = "art/items/";
        constexpr std::string_view CRITTERS  = "art/critters/";
        constexpr std::string_view SCENERY   = "art/scenery/";
        constexpr std::string_view WALLS     = "art/walls/";
        constexpr std::string_view TILES     = "art/tiles/";
        constexpr std::string_view MISC      = "art/misc/";
        constexpr std::string_view INTERFACE = "art/intrface/";
        constexpr std::string_view INVENTORY = "art/inven/";
        
        // Proto directories
        constexpr std::string_view PROTO_ITEMS    = "proto/items/";
        constexpr std::string_view PROTO_CRITTERS = "proto/critters/";
        constexpr std::string_view PROTO_SCENERY  = "proto/scenery/";
        constexpr std::string_view PROTO_WALLS    = "proto/walls/";
        constexpr std::string_view PROTO_TILES    = "proto/tiles/";
        constexpr std::string_view PROTO_MISC     = "proto/misc/";
        
        // Other directories
        constexpr std::string_view MAPS    = "maps/";
        constexpr std::string_view SCRIPTS = "scripts/";
        constexpr std::string_view TEXT    = "text/";
    }

    // ========================================
    // DAT Files - Archive files
    // ========================================
    namespace Dat {
        constexpr std::string_view MASTER  = "master.dat";
        constexpr std::string_view CRITTER = "critter.dat";
    }

} // namespace ResourcePaths
} // namespace geck