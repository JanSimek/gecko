#include "ActionSpec.h"

#include <array>

namespace geck {

namespace {

    // The shipped defaults. Kept in one place so the menus, the toolbar, the canvas shortcuts and
    // the Preferences page all read the same table — see docs/keybindings-plan.md for the rationale
    // behind the families (Alt+digit for panels, Ctrl+digit for elevations, bare letters on the
    // canvas only).
    constexpr std::array<ActionSpec, 34> SPECS = { {
        // File
        { actions::NEW_MAP, "New Map", "File", "", QKeySequence::New, ActionScope::Application },
        { actions::OPEN_MAP, "Open Map", "File", "", QKeySequence::Open, ActionScope::Application },
        { actions::BROWSE_MAPS, "Browse Maps", "File", "Ctrl+B", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::SAVE_MAP, "Save Map", "File", "", QKeySequence::Save, ActionScope::Application },
        { actions::SAVE_MAP_AS, "Save Map As", "File", "", QKeySequence::SaveAs, ActionScope::Application },
        { actions::CLOSE_MAP, "Close Map", "File", "", QKeySequence::Close, ActionScope::Application },
        { actions::PREFERENCES, "Preferences", "File", "", QKeySequence::Preferences, ActionScope::Application },
        { actions::QUIT, "Quit", "File", "", QKeySequence::Quit, ActionScope::Application },
        { actions::PLAY_MAP, "Save and Play in Fallout 2", "File", "F5", QKeySequence::UnknownKey, ActionScope::Application },

        // Edit
        { actions::SELECT_ALL, "Select All", "Edit", "Ctrl+A", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::DESELECT_ALL, "Deselect All", "Edit", "Ctrl+D", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::UNDO, "Undo", "Edit", "", QKeySequence::Undo, ActionScope::Application },
        { actions::REDO, "Redo", "Edit", "", QKeySequence::Redo, ActionScope::Application },
        { actions::EDIT_SCRIPT_SOURCE, "Edit Script Source", "Edit", "Ctrl+Shift+E", QKeySequence::UnknownKey, ActionScope::Application },

        // View
        { actions::SHOW_EXIT_GRIDS, "Show Exit Grids", "View", "Ctrl+E", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::WORLD_MAP, "World Map", "View", "Ctrl+Shift+M", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::SET_ELEVATION_1, "Elevation 1", "View", "Ctrl+1", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::SET_ELEVATION_2, "Elevation 2", "View", "Ctrl+2", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::SET_ELEVATION_3, "Elevation 3", "View", "Ctrl+3", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::HEX_GRID, "Toggle Hex Grid", "View", "G", QKeySequence::UnknownKey, ActionScope::Canvas },

        // Panels
        { actions::PANEL_MAP_INFO, "Map Information Panel", "Panels", "Alt+1", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::PANEL_SELECTION, "Selection Panel", "Panels", "Alt+2", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::PANEL_SCRIPTS, "Scripts Panel", "Panels", "Alt+3", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::PANEL_TILE_PALETTE, "Tile Palette Panel", "Panels", "Alt+4", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::PANEL_OBJECT_PALETTE, "Object Palette Panel", "Panels", "Alt+5", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::PANEL_FILE_BROWSER, "File Browser Panel", "Panels", "Alt+6", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::PANEL_LOG, "Log Panel", "Panels", "Alt+`", QKeySequence::UnknownKey, ActionScope::Application },
        { actions::PANEL_SELECTION_REVEAL, "Inspect Selection", "Panels", "Return", QKeySequence::UnknownKey, ActionScope::Canvas },

        // Navigation
        { actions::CENTER_ON_PLAYER, "Center on Player Start", "Navigation", "Home", QKeySequence::UnknownKey, ActionScope::Canvas },
        { actions::FIT_MAP, "Fit Map in View", "Navigation", "F", QKeySequence::UnknownKey, ActionScope::Canvas },

        // Tools
        { actions::TOOL_SELECT, "Select Mode", "Tools", "S", QKeySequence::UnknownKey, ActionScope::Canvas },
        { actions::TOOL_ROTATE, "Rotate Selected Object", "Tools", "R", QKeySequence::UnknownKey, ActionScope::Canvas },
        { actions::TOOL_SCROLL_BLOCKER_RECT, "Scroll Blocker Rectangle", "Tools", "B", QKeySequence::UnknownKey, ActionScope::Canvas },
        { actions::TOOL_PICK, "Eyedropper", "Tools", "P", QKeySequence::UnknownKey, ActionScope::Canvas },
    } };

} // namespace

std::span<const ActionSpec> actionSpecs() {
    return SPECS;
}

} // namespace geck
