#pragma once

#include <cstdint> // Constants.h uses fixed-width ints without including this itself

#include "util/Constants.h"

namespace geck {

/**
 * @brief Layer/overlay visibility toggles for the editor viewport.
 *
 * Owned by EditorWidget and mapped onto the renderer's settings each frame.
 * Defaults mirror the UI::DEFAULT_* constants.
 */
struct VisibilitySettings {
    bool showObjects = UI::DEFAULT_SHOW_OBJECTS;
    bool showCritters = UI::DEFAULT_SHOW_CRITTERS;
    bool showWalls = UI::DEFAULT_SHOW_WALLS;
    bool showRoof = UI::DEFAULT_SHOW_ROOF;
    bool showScrollBlockers = UI::DEFAULT_SHOW_SCROLL_BLK;
    bool showWallBlockers = UI::DEFAULT_SHOW_WALL_BLK;
    bool showHexGrid = UI::DEFAULT_SHOW_HEX_GRID;
    bool showLightOverlays = false;
    bool showExitGrids = false;
    bool showSpatialScripts = false;
    bool showMapEdges = false;
    bool showUnreachable = false;
    // Merge touching same-category selected objects into one union outline (vs. one per object).
    bool mergeSelectionOutlines = true;
};

} // namespace geck
