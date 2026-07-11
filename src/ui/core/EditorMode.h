#pragma once

namespace geck {

/// The editor's single active tool mode. Exactly one is active at a time;
/// EditorWidget::setMode() is the sole transition point and exits the others.
enum class EditorMode {
    Select,            ///< Default: select/move objects, area-select tiles.
    PlaceTile,         ///< Painting floor/roof tiles from the palette.
    PlaceExitGrid,     ///< Placing exit-grid markers.
    MarkExits,         ///< Selecting exit grids to edit their properties.
    SetPlayerPosition, ///< One-shot: next click sets the player start hex.
    StampPattern,      ///< Placing a loaded prefab pattern by clicking hexes.
    PluginTool,        ///< Dynamic dispatch to the active ToolRegistry tool (object placement today; plugins later).
};

} // namespace geck
