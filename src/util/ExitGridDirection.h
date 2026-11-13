#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "util/Constants.h"

// Picks the direction-specific exit-grid marker art ({proto, frm}) from the drawn line's local segment
// and the destination kind. The PROTO encodes the DIRECTION (which iso edge the marker caps), the FRM
// encodes the FAMILY (green=inter-map vs brown=world/town) + direction; see util/Constants.h for the
// verified 8-direction table. Qt-free + header-only, so it is unit-testable headlessly and shared by
// the "Draw edge" tool and the scripting host.
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

/// The band's OUTWARD screen normal ({dx, dy}, y DOWNWARD): the direction PERPENDICULAR to a marker's
/// on-screen bar, pointing to the side the bar faces. secondRowNeighbor uses it to pick which on-grid
/// neighbour is the SECOND parallel row of a diagonal band (the one leaning most along this normal); a
/// flip (dir ^ 1) reverses the sign, so the second row mirrors to the OTHER side of the hex line.
/// LEFT/RIGHT/BOTTOM/TOP point to their named screen edges.
///
/// The four DIAGONAL vectors point perpendicular to the band's on-screen line (the {±1,±1} screen
/// diagonal runs nearly ALONG the "\" band, so it barely separates the sides). Measured long-axis
/// angles are ~-9° for "/" and ~+26° for "\", so the perpendiculars are ~(1, 6) and ~(1, -2). Only the
/// SIGN/leaning matters here (dot product), so the vectors need not be unit length. Returns {0,0} for
/// an out-of-range dir.
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
            return { +1, +6 }; // "/" side A: perpendicular to the ~-9° "/" band, facing DOWN
        case ExitGrid::DIR_FWD_B:
            return { -1, -6 }; // "/" side B: the dir^1 flip, facing UP
        case ExitGrid::DIR_BACK_A:
            return { -1, +2 }; // "\" side A: perpendicular to the ~+26° "\" band, facing DOWN-LEFT
        case ExitGrid::DIR_BACK_B:
            return { +1, -2 }; // "\" side B: the dir^1 flip, facing UP-RIGHT
        default:
            return { 0, 0 };
    }
}

/// True for the four DIAGONAL directions (the "/" and "\" art families, dir 4..7).
inline bool isDiagonalExitGridDir(int dir) {
    return dir >= ExitGrid::DIR_FWD_A && dir <= ExitGrid::DIR_BACK_B;
}

/// The exit-grid direction (0..7) an art's proto encodes (proto index 16..23 maps 1:1 to dir 0..7).
inline int exitGridDirOfProto(uint32_t proPid) {
    return static_cast<int>(proPid - ExitGrid::FIRST_EXIT_GRID_PID);
}

/// A hex neighbour reduced to its screen offset from the home hex (dx, dy = neighbour - home,
/// y DOWNWARD) plus the neighbour's own hex index, for picking a diagonal band's SECOND row.
struct NeighborOffset {
    int hex = -1;
    int dx = 0;
    int dy = 0;
};

/// Pick the SECOND-ROW neighbour for a diagonal band: of `home`'s grid neighbours, the one whose screen
/// offset leans most strongly along the band's outward normal (exitGridOutward(dir), y DOWNWARD). That
/// places the second row one hex OVER, perpendicular to the band, on the side the bars face — both rows
/// hold REAL, selectable markers. A flip (dir^1) reverses the normal, so the second row mirrors to the
/// other side. Returns -1 if `dir` isn't diagonal or no neighbour leans outward. Because the seed
/// bestDot is 0 and the test is dot > bestDot, a neighbour exactly PERPENDICULAR to the normal (dot 0)
/// is never picked — only one leaning strictly outward. Pure (no grid/SFML) for headless testing.
inline int secondRowNeighbor(int dir, const std::vector<NeighborOffset>& neighbors) {
    if (!isDiagonalExitGridDir(dir)) {
        return -1;
    }
    const auto [outX, outY] = exitGridOutward(dir);
    int best = -1;
    long bestDot = 0;
    for (const NeighborOffset& nb : neighbors) {
        const long dot = static_cast<long>(nb.dx) * outX + static_cast<long>(nb.dy) * outY;
        if (dot > bestDot) {
            bestDot = dot;
            best = nb.hex;
        }
    }
    return best;
}

/// Screen position of the grid's centre hex — the reference point outward facing is measured from.
/// Returns {0,0} for a degenerate grid with no centre hex.
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

    /// The drawn segment's axis. The diagonal band is sized for the Fallout ISO DIAMOND, whose two
    /// slanted edges run along the hex grid's axes with measured SCREEN slopes ~4:1 (shallow NE/SW) and
    /// ~1.33:1 (steep NW/SE) — NOT 1:1. A 45°-centred [1/2, 2] band tips the shallow 4:1 edge to
    /// Horizontal (jagged cardinal bars instead of the "/" "\" art), so the band is |slope| in [1/2, 6]
    /// (slope = |dx|/|dy|): both diamond slopes sit inside while near-pure screen cardinals stay out.
    /// A "/" runs up-right (dx, dy opposite signs), a "\" down-right (same signs). A zero-length segment
    /// reports Horizontal so callers fall back deterministically.
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

    /// Pick the side within an axis pair from the hex's outward facing (outwardX/outwardY = hex - centre,
    /// y DOWNWARD). Matches the verified table (dir0=LEFT, 1=RIGHT, 2=BOTTOM, 3=TOP).
    inline int directionForAxis(SegmentAxis axis, int outwardX, int outwardY) {
        using enum SegmentAxis;
        switch (axis) {
            case Horizontal: // above centre faces TOP, else BOTTOM
                return (outwardY < 0) ? ExitGrid::DIR_TOP : ExitGrid::DIR_BOTTOM;
            case Vertical:
                return (outwardX < 0) ? ExitGrid::DIR_LEFT : ExitGrid::DIR_RIGHT;
            case ForwardSlash: // "/" side B faces up-right (outward up OR right), side A opposite
                return (outwardX > 0 || outwardY < 0) ? ExitGrid::DIR_FWD_B : ExitGrid::DIR_FWD_A;
            case BackSlash: // "\" side B faces down-right (outward down OR right), side A opposite
                return (outwardX > 0 || outwardY > 0) ? ExitGrid::DIR_BACK_B : ExitGrid::DIR_BACK_A;
        }
        return ExitGrid::DIR_BOTTOM;
    }

} // namespace exitgrid_detail

/// Flip a direction (0..7) to the OPPOSITE side of its axis pair (LEFT<->RIGHT, etc). The directions
/// are four adjacent pairs (0/1, 2/3, 4/5, 6/7), so toggling the low bit swaps within the pair and
/// keeps the axis. The two bitmaps in a pair are identical; the flip only changes the sign of
/// exitGridOutward(dir), i.e. which side secondRowNeighbor mirrors the diagonal band's second row to.
/// Out-of-range dirs pass through.
inline int flipExitGridDirection(int dir) {
    if (dir < 0 || dir >= ExitGrid::DIR_COUNT) {
        return dir;
    }
    return dir ^ 1;
}

/// The single direction for a WHOLE drawn stroke. `dx`/`dy` are the stroke's overall screen delta
/// (first->last vertex, y DOWNWARD); `outwardX`/`outwardY` the midpoint's offset from the map centre.
/// The axis picks the pair and the outward facing the side ONCE, so every hex shares one consistent
/// side (a clean continuous bar) instead of flipping mid-run. `flipSide` (the live "flip" key) inverts
/// that side. Returns the direction (0..7); pass to exitGridArtForDirection for the {proto, frm}.
inline int exitGridDirectionForLine(int dx, int dy, int outwardX, int outwardY, bool flipSide) {
    const exitgrid_detail::SegmentAxis axis = exitgrid_detail::classifySegment(dx, dy);
    const int dir = exitgrid_detail::directionForAxis(axis, outwardX, outwardY);
    return flipSide ? flipExitGridDirection(dir) : dir;
}

/// Single-hex fallback (no drawn segment): classify a lone hex by its outward facing alone. Vertical
/// offset dominating => a horizontal (top/bottom) edge, else a vertical (left/right) edge.
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

// --------------------------------------------------------------------------------------------------
// PER-SEGMENT classification (TRUE freeze)
//
// The "Draw edge" tool classifies each polyline SEGMENT from its own screen direction (ONE axis + side,
// uniform within it) and freezes it IMMUTABLY at the click that closes it (hexes + direction captured
// with the flip then in effect, never recomputed; ExitGridPlacementManager owns the frozen segments).
// So Space or a cursor move touches only the ONE live segment. Classifying per SEGMENT (not per hex)
// avoids the both-sides bug a per-hex recompute reintroduces.

/// One polyline segment's geometry for classification: the ordered hexes its hex-line walk passes
/// through (endpoints inclusive), its overall SCREEN delta (last - first hex, y DOWNWARD) for the axis,
/// and the midpoint's outward facing (midpoint - grid centre) for the side. exitGridDirectionForLine
/// maps {screenDx/Dy, outwardX/Y, flipSide} to the single frozen direction.
struct ExitGridSegmentRun {
    std::vector<int> hexes;
    int screenDx = 0;
    int screenDy = 0;
    int outwardX = 0;
    int outwardY = 0;
};

/// Screen position of `hexIndex` on `grid`; {0,0} off-grid.
inline std::pair<int, int> hexScreenPosition(const HexagonGrid& grid, int hexIndex) {
    const auto h = grid.getHexByPosition(static_cast<uint32_t>(hexIndex));
    return h.has_value() ? std::pair<int, int>{ h->get().x(), h->get().y() } : std::pair<int, int>{ 0, 0 };
}

/// The classification geometry of a non-empty ordered hex run: the first->last screen delta and the
/// midpoint's offset from the grid centre.
inline ExitGridSegmentRun exitGridSegmentRun(const HexagonGrid& grid, std::vector<int> hexes) {
    assert(!hexes.empty());
    const auto [centerX, centerY] = hexGridCenterScreen(grid);
    const auto [fx, fy] = hexScreenPosition(grid, hexes.front());
    const auto [lx, ly] = hexScreenPosition(grid, hexes.back());
    const auto [mx, my] = hexScreenPosition(grid, hexes[hexes.size() / 2]);
    ExitGridSegmentRun run;
    run.hexes = std::move(hexes);
    run.screenDx = lx - fx;
    run.screenDy = ly - fy;
    run.outwardX = mx - centerX;
    run.outwardY = my - centerY;
    return run;
}

/// The auto art for a classified segment run: its own screen axis plus its midpoint's outward side,
/// inverted by `flipSide`. The "Draw edge" tool freezes every segment with this.
inline ExitGridArt exitGridArtForRun(const ExitGridSegmentRun& run, ExitGridDestinationKind kind, bool flipSide) {
    return exitGridArtForDirection(
        exitGridDirectionForLine(run.screenDx, run.screenDy, run.outwardX, run.outwardY, flipSide), kind);
}

/// One auto art for a whole stroke: axis from the first to the last hex, side once from the midpoint's
/// outward facing, so every hex shares one side (a clean continuous bar) instead of flipping mid-run.
/// `flipSide` inverts the side. A lone hex has no axis and classifies by its outward facing, the flip
/// still applied; a null grid or empty stroke yields a bottom-edge marker.
inline ExitGridArt exitGridArtForStroke(const HexagonGrid* grid, const std::vector<int>& orderedHexes,
    ExitGridDestinationKind kind, bool flipSide) {
    if (grid == nullptr || orderedHexes.empty()) {
        return exitGridArtForDirection(ExitGrid::DIR_BOTTOM, kind);
    }
    if (orderedHexes.size() < 2) {
        const auto [centerX, centerY] = hexGridCenterScreen(*grid);
        const auto [hx, hy] = hexScreenPosition(*grid, orderedHexes.front());
        const ExitGridArt art = exitGridArtForFacing(hx, hy, centerX, centerY, kind);
        return flipSide ? exitGridArtForDirection(flipExitGridDirection(exitGridDirOfProto(art.proPid)), kind)
                        : art;
    }
    return exitGridArtForRun(exitGridSegmentRun(*grid, orderedHexes), kind, flipSide);
}

/// How many rows over a diagonal band's second row sits. A diagonal bar is ~32-38px against a ~16-19px
/// hex step, so 2 tiles the two centred rows edge-to-edge into a clean ~2x band; 1 overlaps them ~50%.
inline constexpr int EXIT_GRID_SECOND_ROW_STEPS = 2;

/// The hex `steps` rows over from `hex`, perpendicular to a diagonal band on the side
/// exitGridOutward(dir) faces: each step takes the neighbour secondRowNeighbor picks. -1 if a step walks
/// off-grid or no neighbour leans outward.
inline int exitGridSecondRowHex(const HexagonGrid& grid, int hex, int dir, int steps) {
    int current = hex;
    for (int i = 0; i < steps && current >= 0; ++i) {
        const auto here = grid.getHexByPosition(static_cast<uint32_t>(current));
        if (!here.has_value()) {
            return -1;
        }
        const int hx = here->get().x();
        const int hy = here->get().y();
        std::vector<NeighborOffset> offsets;
        for (const int nb : hexgrid::neighbors(current)) {
            if (const auto nbHex = grid.getHexByPosition(static_cast<uint32_t>(nb)); nbHex.has_value()) {
                offsets.push_back({ nb, nbHex->get().x() - hx, nbHex->get().y() - hy });
            }
        }
        current = secondRowNeighbor(dir, offsets);
    }
    return current;
}

/// The hexes a segment of direction `dir` covers. A diagonal edge is a 2-deep band: the drawn line is
/// the outer row and a second parallel row of real markers sits EXIT_GRID_SECOND_ROW_STEPS hexes over,
/// appended after it and deduped. A cardinal edge stays the single drawn row.
inline std::vector<int> exitGridBandHexes(const HexagonGrid& grid, const std::vector<int>& lineHexes, int dir) {
    std::vector<int> hexes = lineHexes;
    if (!isDiagonalExitGridDir(dir)) {
        return hexes;
    }
    std::set<int> seen(lineHexes.begin(), lineHexes.end());
    for (const int hex : lineHexes) {
        const int second = exitGridSecondRowHex(grid, hex, dir, EXIT_GRID_SECOND_ROW_STEPS);
        if (second >= 0 && seen.insert(second).second) {
            hexes.push_back(second);
        }
    }
    return hexes;
}

/// One immutably-captured polyline segment: its hexes and the parallel per-hex art it was frozen with.
struct ExitGridSegment {
    std::vector<int> hexes;
    std::vector<ExitGridArt> art;
};

/// Flatten the frozen segments (in commit order) then the live one into deduped parallel vectors.
/// First-seen wins, so committed hexes and art stay pixel-stable as the live segment moves.
inline void flattenExitGridSegments(const std::vector<ExitGridSegment>& committed, const ExitGridSegment& live,
    std::vector<int>& outHexes, std::vector<ExitGridArt>& outArt) {
    outHexes.clear();
    outArt.clear();
    std::set<int> seen;
    const auto append = [&](const ExitGridSegment& seg) {
        for (std::size_t i = 0; i < seg.hexes.size(); ++i) {
            if (seen.insert(seg.hexes[i]).second) {
                outHexes.push_back(seg.hexes[i]);
                outArt.push_back(seg.art[i]);
            }
        }
    };
    for (const ExitGridSegment& seg : committed) {
        append(seg);
    }
    append(live);
}

// --------------------------------------------------------------------------------------------------
// SHIFT-SNAP to clean exit-grid angles
//
// Holding Shift snaps the LIVE segment onto the nearest CLEAN exit-grid angle so the placed bars line
// up instead of staircasing. The eight clean angles are the screen directions of the exit-grid edges
// (y DOWNWARD): horizontal (±1, 0), vertical (0, ±1), the shallow 4:1 iso edge (±4, ∓1), and the steep
// 4/3 iso edge (±4, ±3). These are the SAME slopes the classifier's iso-diamond band uses, so a snapped
// segment classifies cleanly to one art for its whole length.

/// One clean snap direction: a SCREEN-space unit vector (y DOWNWARD).
struct ExitGridSnapDir {
    double x;
    double y;
};

/// The eight clean exit-grid screen directions as unit vectors: the measured iso slopes (4:1 shallow,
/// 4/3 steep) plus the two screen cardinals. y grows DOWNWARD.
inline const std::array<ExitGridSnapDir, 8>& exitGridSnapDirections() {
    // 1/sqrt(17) for the 4:1 shallow edge, 1/5 for the 3-4-5 steep edge.
    static constexpr double kShallow = 0.242535625036333; // 1 / sqrt(4^2 + 1^2)
    static constexpr double kSteep = 0.2;                 // 1 / sqrt(4^2 + 3^2)
    static const std::array<ExitGridSnapDir, 8> dirs = { {
        { 1.0, 0.0 },                        // horizontal +x
        { -1.0, 0.0 },                       // horizontal -x
        { 0.0, 1.0 },                        // vertical +y (down)
        { 0.0, -1.0 },                       // vertical -y (up)
        { 4.0 * kShallow, -1.0 * kShallow }, // shallow "/" up-right
        { -4.0 * kShallow, 1.0 * kShallow }, // shallow "/" down-left
        { 4.0 * kSteep, 3.0 * kSteep },      // steep "\" down-right
        { -4.0 * kSteep, -3.0 * kSteep },    // steep "\" up-left
    } };
    return dirs;
}

/// Snap a live-segment endpoint to the nearest clean exit-grid angle. `lastX/lastY` is the last vertex,
/// `cursorX/cursorY` the raw cursor. Returns the cursor rotated onto the nearest clean direction while
/// KEEPING its distance from the last vertex (project onto the best-matching ray, re-emit at the same
/// length). A zero-length offset is returned unchanged. Pure (no SFML/Qt) for headless testing.
inline std::pair<double, double> snapToExitGridAngle(double lastX, double lastY,
    double cursorX, double cursorY) {
    const double dx = cursorX - lastX;
    const double dy = cursorY - lastY;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0) {
        return { cursorX, cursorY };
    }
    // Pick the ray the offset projects onto most strongly (max dot product); since the eight directions
    // include both signs of each axis, that is the nearest clean angle.
    const auto& dirs = exitGridSnapDirections();
    double bestDot = -1e300;
    ExitGridSnapDir best = dirs.front();
    for (const ExitGridSnapDir& d : dirs) {
        const double dot = dx * d.x + dy * d.y;
        if (dot > bestDot) {
            bestDot = dot;
            best = d;
        }
    }
    // Re-emit at the ORIGINAL distance along the chosen unit ray (length preserved, angle snapped).
    return { lastX + best.x * len, lastY + best.y * len };
}

} // namespace geck
