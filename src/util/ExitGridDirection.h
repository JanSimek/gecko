#pragma once

#include <cstdint>
#include <cstdlib>
#include <utility>

#include "editor/HexagonGrid.h"
#include "util/Constants.h"

// Picks the direction-specific exit-grid marker art ({proto, frm}) for a hex from the line the user
// drew through it (its local segment) and the destination kind. Exit grids run along the map's iso
// edges; the PROTO encodes the DIRECTION (which iso edge the marker caps) and the FRM encodes the
// FAMILY (green=inter-map vs brown=world/town) + direction. See util/Constants.h for the verified
// 8-direction table. Qt-free and header-only, so it is unit-testable headlessly and reused by both
// the editor's "Draw edge" tool and the scripting host.
namespace geck {

struct ExitGridArt {
    uint32_t proPid;
    uint32_t frmPid;
};

/// Which art family a marker draws in: an inter-map exit (a real exit_map) is GREEN; a world/town
/// exit (exit_map == WORLD_MAP_EXIT or TOWN_MAP_EXIT) is BROWN.
enum class ExitGridDestinationKind {
    InterMap, ///< green art (exitgrd*, frm = proto + 1)
    WorldMap, ///< brown art (ext2grd*, frm = proto + 0x11)
};

/// The {proto, frm} for direction `dir` (0..7, an ExitGrid::Direction) in the given family. The proto
/// is the directional MISC proto (index 16..23); the frm is green or brown by destination kind.
inline ExitGridArt exitGridArtForDirection(int dir, ExitGridDestinationKind kind) {
    const uint32_t proto = ExitGrid::exitGridProto(dir);
    const uint32_t frm = (kind == ExitGridDestinationKind::WorldMap)
        ? ExitGrid::brownFrm(dir)
        : ExitGrid::greenFrm(dir);
    return { proto, frm };
}

/// The screen position of the hex grid's centre hex, the reference point outward facing is measured
/// from. Returns {0,0} if the grid has no centre hex (degenerate).
inline std::pair<int, int> hexGridCenterScreen(const HexagonGrid& grid) {
    const int centerHex = (HexagonGrid::GRID_HEIGHT / 2) * HexagonGrid::GRID_WIDTH
        + (HexagonGrid::GRID_WIDTH / 2);
    if (const auto h = grid.getHexByPosition(static_cast<uint32_t>(centerHex)); h.has_value()) {
        return { h->get().x(), h->get().y() };
    }
    return { 0, 0 };
}

namespace exitgrid_detail {

    /// Classify a drawn segment (dx, dy in screen space, y growing DOWNWARD) into an axis:
    /// near-diagonal (|dx| ~ |dy|), horizontal (|dx| dominates), or vertical (|dy| dominates).
    enum class SegmentAxis { Horizontal,
        Vertical,
        ForwardSlash,
        BackSlash };

    /// The drawn segment's axis. A segment counts as diagonal when neither screen component clearly
    /// dominates (|dx| and |dy| within a 2:1 band of each other); a "/" runs up-right (dx, dy
    /// opposite signs in screen space), a "\" runs down-right (dx, dy same signs). Outside the
    /// diagonal band the longer screen component wins: |dx| >= |dy| is a horizontal line (top/bottom
    /// bars), otherwise a vertical line (left/right bars). A zero-length segment is reported
    /// Horizontal so callers fall back deterministically.
    inline SegmentAxis classifySegment(int dx, int dy) {
        using enum SegmentAxis;
        const int adx = std::abs(dx);
        const int ady = std::abs(dy);
        if (adx == 0 && ady == 0) {
            return Horizontal;
        }
        // Diagonal band: neither component more than ~2x the other.
        if (const bool nearDiagonal = adx <= 2 * ady && ady <= 2 * adx; nearDiagonal) {
            // Opposite signs => up-right "/"; same signs (incl. one axis zero handled above) => "\".
            const bool opposite = (dx > 0) != (dy > 0);
            return opposite ? ForwardSlash : BackSlash;
        }
        return (adx >= ady) ? Horizontal : Vertical;
    }

    /// Pick the side within an axis pair from the hex's outward facing (which way it points away from
    /// the map centre: outwardX/outwardY = hex - centre, y DOWNWARD). The first index of each pair is
    /// the LEFT/BOTTOM/"/"-A/"\"-A side; the second is the opposite — matching the verified table
    /// (dir0=LEFT, dir1=RIGHT, dir2=BOTTOM, dir3=TOP).
    inline int directionForAxis(SegmentAxis axis, int outwardX, int outwardY) {
        using enum SegmentAxis;
        switch (axis) {
            case Horizontal:
                // A horizontal drawn line is a top or bottom edge: above centre faces TOP, else BOTTOM.
                return (outwardY < 0) ? ExitGrid::DIR_TOP : ExitGrid::DIR_BOTTOM;
            case Vertical:
                // A vertical drawn line is a left or right edge.
                return (outwardX < 0) ? ExitGrid::DIR_LEFT : ExitGrid::DIR_RIGHT;
            case ForwardSlash:
                // "/" pair (dir 4/5): side B faces up-right (outward up OR right), side A the opposite.
                return (outwardX > 0 || outwardY < 0) ? ExitGrid::DIR_FWD_B : ExitGrid::DIR_FWD_A;
            case BackSlash:
                // "\" pair (dir 6/7): side B faces down-right (outward down OR right), side A opposite.
                return (outwardX > 0 || outwardY > 0) ? ExitGrid::DIR_BACK_B : ExitGrid::DIR_BACK_A;
        }
        return ExitGrid::DIR_BOTTOM;
    }

} // namespace exitgrid_detail

/// The directional exit-grid art for a hex drawn as part of a line. `dx`/`dy` are the hex's local
/// segment direction in screen space (y DOWNWARD); `outwardX`/`outwardY` are the hex's offset from
/// the map centre (which way it faces away from centre); `kind` selects green vs brown. The segment
/// axis picks the direction pair (horizontal/vertical/"/"/"\"), and the outward facing picks the side
/// within the pair. The user's Marker-Direction override can force a specific direction instead.
inline ExitGridArt exitGridArtForSegment(int dx, int dy, int outwardX, int outwardY,
    ExitGridDestinationKind kind) {
    const exitgrid_detail::SegmentAxis axis = exitgrid_detail::classifySegment(dx, dy);
    const int dir = exitgrid_detail::directionForAxis(axis, outwardX, outwardY);
    return exitGridArtForDirection(dir, kind);
}

/// Single-hex fallback (no drawn segment): classify a lone hex purely by its outward facing from the
/// map centre. The vertical offset dominating => a horizontal (top/bottom) edge, else a vertical
/// (left/right) edge. Used by single-hex placement, where there is no line to take an axis from.
inline ExitGridArt exitGridArtForFacing(int hexX, int hexY, int mapCenterX, int mapCenterY,
    ExitGridDestinationKind kind) {
    const int dx = hexX - mapCenterX;
    const int dy = hexY - mapCenterY;
    const exitgrid_detail::SegmentAxis axis = (std::abs(dy) >= std::abs(dx))
        ? exitgrid_detail::SegmentAxis::Horizontal
        : exitgrid_detail::SegmentAxis::Vertical;
    const int dir = exitgrid_detail::directionForAxis(axis, dx, dy);
    return exitGridArtForDirection(dir, kind);
}

} // namespace geck
