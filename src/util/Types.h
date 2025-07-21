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
    ALL,            ///< Select any element type (mixed selection allowed)
    FLOOR_TILES,    ///< Select only floor tiles
    ROOF_TILES,     ///< Select only roof tiles (with textures)
    ROOF_TILES_ALL, ///< Select all roof tiles including empty ones
    OBJECTS,        ///< Select only objects

    NUM_SELECTION_TYPES ///< Total number of selection modes (for cycling)
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
        default:
            return "Unknown";
    }
}

} // namespace geck