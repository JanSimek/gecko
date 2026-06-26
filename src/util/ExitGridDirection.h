#pragma once

#include <cstdint>
#include <cstdlib>

#include "editor/HexagonGrid.h"
#include "util/Constants.h"

// Picks the direction-specific exit-grid marker art for a hex by which way it faces away from
// the map centre. Exit grids run along the map's iso edges, and each edge uses a different art
// (the RECT_* pieces) so the marker points outward. Qt-free and free-function, so it is
// unit-testable headlessly and reused by both the editor's "Draw edge" tool and any host code.
namespace geck {

struct ExitGridArt {
    uint32_t proPid;
    uint32_t frmPid;
};

/// The directional exit-grid art for a hex at screen (hexX, hexY) relative to the map's screen
/// centre (mapCenterX, mapCenterY). Screen y grows DOWNWARD, so a hex above centre (dy < 0) faces
/// the TOP edge. When the vertical offset dominates (|dy| >= |dx|) the hex is on a horizontal
/// (top/bottom) edge; otherwise it is on a vertical (left/right) edge. The destination is stored
/// separately (exit_map/position/elevation/orientation) — the art is purely directional.
inline ExitGridArt exitGridArtForFacing(int hexX, int hexY, int mapCenterX, int mapCenterY) {
    const int dx = hexX - mapCenterX;
    const int dy = hexY - mapCenterY;
    if (std::abs(dy) >= std::abs(dx)) {
        return dy < 0
            ? ExitGridArt{ ExitGrid::RECT_TOP_PRO_PID, ExitGrid::RECT_TOP_FRM_PID }
            : ExitGridArt{ ExitGrid::RECT_BOTTOM_PRO_PID, ExitGrid::RECT_BOTTOM_FRM_PID };
    }
    return dx < 0
        ? ExitGridArt{ ExitGrid::RECT_LEFT_PRO_PID, ExitGrid::RECT_LEFT_FRM_PID }
        : ExitGridArt{ ExitGrid::RECT_RIGHT_PRO_PID, ExitGrid::RECT_RIGHT_FRM_PID };
}

/// The screen position of the hex grid's centre hex, used as the reference point classify(...)
/// measures outward facing from. Returns {0,0} if the grid has no centre hex (degenerate).
inline std::pair<int, int> hexGridCenterScreen(const HexagonGrid& grid) {
    const int centerHex = (HexagonGrid::GRID_HEIGHT / 2) * HexagonGrid::GRID_WIDTH
        + (HexagonGrid::GRID_WIDTH / 2);
    if (const auto h = grid.getHexByPosition(static_cast<uint32_t>(centerHex)); h.has_value()) {
        return { h->get().x(), h->get().y() };
    }
    return { 0, 0 };
}

} // namespace geck
