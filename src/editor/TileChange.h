#pragma once

#include <cstdint>

namespace geck {

/**
 * @brief A single floor/roof tile edit (before/after value at a tile), recorded for undo/redo.
 *
 * The one tile-edit record shared across the editor: tile placement/fill/replace, and moving a
 * selected roof region (each moved tile is expressed as a clear-source + set-target pair). Lives in
 * the gecko_core editor layer so both the selection code and the UI can produce and consume it.
 */
struct TileChange {
    int elevation;
    int tileIndex;
    bool isRoof;
    uint16_t before;
    uint16_t after;
};

} // namespace geck
