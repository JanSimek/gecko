#pragma once

namespace geck {

/**
 * @brief Selection mode for the map editor
 *
 * Defines what types of elements can be selected in the editor:
 * - ALL: Allow selection of any element type (objects, floor tiles, roof tiles)
 * - FLOOR_TILES: Only allow selection of floor tiles
 * - ROOF_TILES: Only allow selection of roof tiles (only tiles with textures)
 * - ROOF_TILES_ALL: Allow selection of all roof tiles including empty ones
 * - OBJECTS: Only allow selection of objects
 */
enum class SelectionMode : int {
    ALL,                      ///< Select any element type (mixed selection allowed)
    FLOOR_TILES,              ///< Select only floor tiles
    ROOF_TILES,               ///< Select only roof tiles (with textures)
    ROOF_TILES_ALL,           ///< Select all roof tiles including empty ones
    OBJECTS,                  ///< Select only objects
    HEXES,                    ///< Select only hexes (for wall blocker placement)
    SCROLL_BLOCKER_RECTANGLE, ///< Draw rectangle and place scroll blockers on borders

    NUM_SELECTION_TYPES ///< Total number of selection modes (for cycling)
};

/**
 * @brief The combinable layer categories of a mixed (ALL-mode) selection.
 *
 * Lets the user pick any combination of layers to select (e.g. roof + floor tiles) instead of
 * one hardcoded SelectionMode at a time. In ALL mode a disabled layer is treated as absent:
 * area-select, the single-click cycle and Ctrl-deselect all skip it. The dedicated single-layer
 * modes (FLOOR_TILES/ROOF_TILES/OBJECTS) are unaffected. Default: every layer on (classic ALL).
 */
struct SelectionLayers {
    bool floorTiles = true;
    bool roofTiles = true;
    bool objects = true;

    bool any() const { return floorTiles || roofTiles || objects; }
    bool all() const { return floorTiles && roofTiles && objects; }
};

/**
 * @brief Convert SelectionMode enum to human-readable string
 * @param mode The selection mode to convert
 * @return String representation of the selection mode
 */
inline const char* selectionModeToString(SelectionMode mode) {
    switch (mode) {
        case SelectionMode::ALL:
            return "All";
        case SelectionMode::FLOOR_TILES:
            return "Floor Tiles";
        case SelectionMode::ROOF_TILES:
            return "Roof Tiles";
        case SelectionMode::ROOF_TILES_ALL:
            return "Roof Tiles + Empty";
        case SelectionMode::OBJECTS:
            return "Objects";
        case SelectionMode::HEXES:
            return "Hexes";
        case SelectionMode::SCROLL_BLOCKER_RECTANGLE:
            return "Scroll Blocker";
        default:
            return "Unknown";
    }
}

} // namespace geck