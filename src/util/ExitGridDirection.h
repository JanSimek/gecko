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

/// The OUTWARD screen direction (toward the map boundary / off-map) for a marker of direction `dir`
/// (0..7), as a unit-ish {dx, dy} with screen y growing DOWNWARD. This is the direction the marker's
/// bar art should extend so the trigger hex sits at the bar's INNER (player-facing) edge — a player
/// walking out from the interior enters the bar at the trigger and leaves the map. It is the same
/// notion of "which iso edge a direction caps" the classifier uses (verified against bhrnddst.map's
/// brown exit rectangle): LEFT/RIGHT cap the screen-left/right edges, BOTTOM/TOP the screen-bottom/top
/// edges, and the diagonals slant along their iso edge. Returns {0,0} for an out-of-range dir.
inline std::pair<int, int> exitGridOutward(int dir) {
    switch (dir) {
        case ExitGrid::DIR_LEFT:
            return { -1, 0 }; // caps the screen-left edge: bar extends left
        case ExitGrid::DIR_RIGHT:
            return { +1, 0 }; // caps the screen-right edge: bar extends right
        case ExitGrid::DIR_BOTTOM:
            return { 0, +1 }; // caps the screen-bottom edge: bar extends down
        case ExitGrid::DIR_TOP:
            return { 0, -1 }; // caps the screen-top edge: bar extends up
        case ExitGrid::DIR_FWD_A:
            return { -1, +1 }; // "/" side A faces down-left
        case ExitGrid::DIR_FWD_B:
            return { +1, -1 }; // "/" side B faces up-right
        case ExitGrid::DIR_BACK_A:
            return { -1, -1 }; // "\" side A faces up-left
        case ExitGrid::DIR_BACK_B:
            return { +1, +1 }; // "\" side B faces down-right
        default:
            return { 0, 0 };
    }
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

    /// The drawn segment's axis. The diagonal band is sized for the Fallout ISO DIAMOND: the playable
    /// area is a diamond whose two slanted edges run along the hex grid's two axes, and those edges
    /// have measured SCREEN slopes of ~1.33:1 (the steep NW/SE edge) and ~4:1 (the shallow NE/SW edge)
    /// — NOT 1:1. A 45°-centred [1/2, 2] band tips the shallow 4:1 edge to Horizontal, so a stroke
    /// drawn along the real diamond edge gets jagged cardinal bars instead of the diagonal "/" "\" art.
    /// So the band is |slope| in [1/2, 6] (slope = |dx|/|dy|), which puts BOTH diamond-edge slopes
    /// comfortably inside (4:1 with a 1.5x margin to the upper bound) while still excluding a near-pure
    /// screen-horizontal stroke (|slope| > 6 => Horizontal) and a near-pure screen-vertical stroke
    /// (|slope| < 1/2 => Vertical). A "/" runs up-right (dx, dy opposite signs in screen space), a "\"
    /// runs down-right (dx, dy same signs). A zero-length segment is reported Horizontal so callers
    /// fall back deterministically.
    inline SegmentAxis classifySegment(int dx, int dy) {
        using enum SegmentAxis;
        const int adx = std::abs(dx);
        const int ady = std::abs(dy);
        if (adx == 0 && ady == 0) {
            return Horizontal;
        }
        // Diagonal band: |slope| in [1/2, 6]. The upper bound (adx <= 6*ady) admits the shallow 4:1
        // diamond edge; the lower bound (ady <= 2*adx) admits the steep 1.33:1 one.
        if (const bool nearDiagonal = adx <= 6 * ady && ady <= 2 * adx; nearDiagonal) {
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

/// Flip a direction (0..7) to the OPPOSITE side of its axis pair: LEFT<->RIGHT, BOTTOM<->TOP,
/// "/"-A<->"/"-B, "\"-A<->"\"-B. The eight directions are laid out as four adjacent pairs
/// (0/1, 2/3, 4/5, 6/7), so toggling the low bit swaps within the pair and keeps the axis. The two
/// art bitmaps in a pair are identical; the only on-screen difference is which side the renderer's
/// outward offset (exitGridOutward) pushes the bar to — so a flip moves the bar to the other side
/// (e.g. a horizontal edge's bar from south to north). Out-of-range dirs are returned unchanged.
inline int flipExitGridDirection(int dir) {
    if (dir < 0 || dir >= ExitGrid::DIR_COUNT) {
        return dir;
    }
    return dir ^ 1;
}

/// The single direction for a WHOLE drawn stroke. `dx`/`dy` are the stroke's overall screen delta
/// (first vertex -> last vertex, y DOWNWARD); `outwardX`/`outwardY` are the stroke midpoint's offset
/// from the map centre (which way the whole edge faces away from centre). The stroke's axis picks the
/// pair and the overall outward facing picks the side ONCE — so every hex on the stroke gets the same
/// direction (one consistent side, a clean continuous bar) instead of a per-hex recompute that flips
/// sides mid-run. `flipSide` inverts that side within the pair (the live "flip" key), so the user can
/// put the edge on the opposite side (south<->north, left<->right, ...). Returns the chosen direction
/// (0..7); pass it to exitGridArtForDirection for the {proto, frm}.
inline int exitGridDirectionForLine(int dx, int dy, int outwardX, int outwardY, bool flipSide) {
    const exitgrid_detail::SegmentAxis axis = exitgrid_detail::classifySegment(dx, dy);
    const int dir = exitgrid_detail::directionForAxis(axis, outwardX, outwardY);
    return flipSide ? flipExitGridDirection(dir) : dir;
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
