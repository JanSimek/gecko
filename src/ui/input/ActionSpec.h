#pragma once

#include <QKeySequence>
#include <span>

namespace geck {

/// Where a binding is listened for.
///
/// Application shortcuts are window-scoped QAction shortcuts: they fire wherever focus is.
/// Canvas shortcuts hang off the map view (Qt::WidgetWithChildrenShortcut) and fire only while it
/// has focus, which is what keeps a single letter from being stolen out of a palette grid or a
/// tree. The two are not independent namespaces — an Application binding fires while the canvas
/// has focus too — so conflicts are checked across both.
enum class ActionScope {
    Application,
    Canvas
};

/// One row of the shipped keyboard-shortcut table.
struct ActionSpec {
    /// Stable identifier. Persisted in settings.json, so it is never renamed once shipped.
    const char* id;
    /// What the Preferences page shows, e.g. "Selection Panel".
    const char* label;
    /// Preferences grouping: "File" | "Edit" | "View" | "Panels" | "Navigation" | "Tools".
    const char* category;
    /// QKeySequence portable text ("Ctrl+S", "Alt+1"); empty means "use standardKey".
    const char* defaultKeys;
    /// Set instead of defaultKeys where Qt's platform-correct binding is the default: several of
    /// these differ per platform (Preferences is Ctrl+, only on macOS, Quit is unbound on Windows),
    /// so resolving at runtime keeps each platform's convention instead of freezing one of them
    /// into the table. UnknownKey with empty defaultKeys means unbound by default.
    QKeySequence::StandardKey standardKey = QKeySequence::UnknownKey;
    ActionScope scope = ActionScope::Application;
};

/// The shipped table, in the order the Preferences page lists it.
///
/// Ids are strings rather than an enum because they are what lands in settings.json: a reordered
/// enum would silently repoint every user's overrides at the wrong action.
std::span<const ActionSpec> actionSpecs();

/// Action ids. Compile-time names for the rows above, so a typo is a build error rather than a
/// binding that silently never fires.
namespace actions {
    inline constexpr const char* NEW_MAP = "file.newMap";
    inline constexpr const char* OPEN_MAP = "file.openMap";
    inline constexpr const char* BROWSE_MAPS = "file.browseMaps";
    inline constexpr const char* SAVE_MAP = "file.saveMap";
    inline constexpr const char* SAVE_MAP_AS = "file.saveMapAs";
    inline constexpr const char* CLOSE_MAP = "file.closeMap";
    inline constexpr const char* PREFERENCES = "file.preferences";
    inline constexpr const char* QUIT = "file.quit";
    inline constexpr const char* PLAY_MAP = "file.playMap";

    inline constexpr const char* SELECT_ALL = "edit.selectAll";
    inline constexpr const char* DESELECT_ALL = "edit.deselectAll";
    inline constexpr const char* UNDO = "edit.undo";
    inline constexpr const char* REDO = "edit.redo";
    inline constexpr const char* EDIT_SCRIPT_SOURCE = "edit.scriptSource";

    inline constexpr const char* SHOW_EXIT_GRIDS = "view.exitGrids";
    inline constexpr const char* WORLD_MAP = "view.worldMap";
    inline constexpr const char* HEX_GRID = "view.hexGrid";
    inline constexpr const char* ELEVATION_1 = "view.elevation1";
    inline constexpr const char* ELEVATION_2 = "view.elevation2";
    inline constexpr const char* ELEVATION_3 = "view.elevation3";

    inline constexpr const char* PANEL_MAP_INFO = "panel.mapInfo";
    inline constexpr const char* PANEL_SELECTION = "panel.selection";
    inline constexpr const char* PANEL_SCRIPTS = "panel.scripts";
    inline constexpr const char* PANEL_TILE_PALETTE = "panel.tilePalette";
    inline constexpr const char* PANEL_OBJECT_PALETTE = "panel.objectPalette";
    inline constexpr const char* PANEL_FILE_BROWSER = "panel.fileBrowser";
    inline constexpr const char* PANEL_LOG = "panel.log";
    inline constexpr const char* PANEL_SELECTION_REVEAL = "panel.selection.reveal";

    inline constexpr const char* CENTER_ON_PLAYER = "view.centerOnPlayer";
    inline constexpr const char* FIT_MAP = "view.fitMap";

    inline constexpr const char* TOOL_SELECT = "tool.select";
    inline constexpr const char* TOOL_ROTATE = "tool.rotate";
    inline constexpr const char* TOOL_SCROLL_BLOCKER_RECT = "tool.scrollBlockerRect";
    inline constexpr const char* TOOL_PICK = "tool.pick";
} // namespace actions

} // namespace geck
