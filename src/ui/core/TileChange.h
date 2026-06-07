#pragma once

#include <cstdint>

namespace geck {

/**
 * @brief A single floor/roof tile edit, recorded for undo/redo.
 *
 * Promoted out of EditorWidget so it can be forward-declared by the
 * TilePlacementContext interface and shared between the editor and the
 * tile placement manager without a compile cycle.
 */
struct TileChange {
    int elevation;
    int tileIndex;
    bool isRoof;
    uint16_t before;
    uint16_t after;
};

} // namespace geck
