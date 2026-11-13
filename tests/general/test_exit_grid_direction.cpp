#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "util/Constants.h"
#include "util/ExitGridDirection.h"
#include "util/HexLine.h"

using geck::ExitGridArt;
using geck::exitGridArtForDirection;
using geck::exitGridArtForFacing;
using geck::exitGridArtForRun;
using geck::exitGridArtForStroke;
using geck::exitGridBandHexes;
using geck::ExitGridDestinationKind;
using geck::exitGridDirectionForLine;
using geck::exitGridDirOfProto;
using geck::exitGridOutward;
using geck::ExitGridSegment;
using geck::ExitGridSegmentRun;
using geck::exitGridSnapDirections;
using geck::flattenExitGridSegments;
using geck::flipExitGridDirection;
using geck::HexagonGrid;
using geck::isDiagonalExitGridDir;
using geck::NeighborOffset;
using geck::secondRowNeighbor;
using geck::snapToExitGridAngle;
namespace ExitGrid = geck::ExitGrid;

namespace {
// Reference point the facing is measured from.
constexpr int CX = 1000;
constexpr int CY = 1000;
constexpr auto INTER = ExitGridDestinationKind::InterMap; // green
constexpr auto WORLD = ExitGridDestinationKind::WorldMap; // brown

// Every emitted proto must be one of the eight directional exit-grid protos (index 16..23).
bool isExitGridProto(uint32_t proPid) {
    return proPid >= ExitGrid::FIRST_EXIT_GRID_PID && proPid <= ExitGrid::LAST_EXIT_GRID_PID;
}

// The art a drawn stroke gets, unflipped: the line's direction rendered for one destination kind.
ExitGridArt artForLine(int dx, int dy, int outwardX, int outwardY, ExitGridDestinationKind kind) {
    return exitGridArtForDirection(exitGridDirectionForLine(dx, dy, outwardX, outwardY, false), kind);
}

// Assert both destination kinds emit an in-range proto for one segment, across every outward facing.
void checkSegmentProtosInRange(int dx, int dy) {
    for (int ox = -1; ox <= 1; ++ox) {
        for (int oy = -1; oy <= 1; ++oy) {
            const ExitGridArt g = artForLine(dx, dy, ox, oy, INTER);
            const ExitGridArt b = artForLine(dx, dy, ox, oy, WORLD);
            CHECK(isExitGridProto(g.proPid));
            CHECK(isExitGridProto(b.proPid));
        }
    }
}
} // namespace

TEST_CASE("Each exit-grid direction's green art is its proto + 1 and its brown art its proto + 0x11", "[exit_grid]") {
    for (int dir = 0; dir < ExitGrid::DIR_COUNT; ++dir) {
        const uint32_t proto = ExitGrid::exitGridProto(dir);
        CHECK(proto == 0x05000010u + static_cast<uint32_t>(dir));
        CHECK(ExitGrid::greenFrm(dir) == proto + 1);
        CHECK(ExitGrid::brownFrm(dir) == proto + 0x11);
    }
}

TEST_CASE("exitGridArtForDirection emits green for inter-map and brown for world/town", "[exit_grid]") {
    for (int dir = 0; dir < ExitGrid::DIR_COUNT; ++dir) {
        const ExitGridArt green = exitGridArtForDirection(dir, INTER);
        const ExitGridArt brown = exitGridArtForDirection(dir, WORLD);
        // Same proto (= direction), different family.
        CHECK(green.proPid == ExitGrid::exitGridProto(dir));
        CHECK(brown.proPid == ExitGrid::exitGridProto(dir));
        CHECK(green.frmPid == ExitGrid::greenFrm(dir));
        CHECK(brown.frmPid == ExitGrid::brownFrm(dir));
        // Inter-map vs world flips green <-> brown.
        CHECK(green.frmPid != brown.frmPid);
        CHECK(isExitGridProto(green.proPid));
    }
}

// Verified data points: a horizontal line for a world/town exit -> proto 0x05000013 (dir 3, TOP), brown
// frm 0x05000024; a "/" diagonal -> proto 0x05000014 (dir 4), brown frm 0x05000025.
TEST_CASE("A horizontal world-map exit line takes the top proto 0x05000013 and brown art 0x05000024", "[exit_grid]") {
    // A horizontal screen segment (dx dominates), hex above centre -> TOP edge, world map -> brown.
    const ExitGridArt art = artForLine(/*dx=*/200, /*dy=*/0,
        /*outwardX=*/0, /*outwardY=*/-300, WORLD);
    CHECK(art.proPid == 0x05000013u);
    CHECK(art.frmPid == 0x05000024u);
}

TEST_CASE("A '/' world-map exit line facing down-left takes proto 0x05000014 and brown art 0x05000025", "[exit_grid]") {
    // A "/" segment runs up-right: dx > 0, dy < 0 in screen space. side B faces up-right (dir 5);
    // a hex facing down-left takes side A (dir 4). World map -> brown ext2grd5 = 0x05000025.
    const ExitGridArt art = artForLine(/*dx=*/120, /*dy=*/-120,
        /*outwardX=*/-200, /*outwardY=*/200, WORLD);
    CHECK(art.proPid == 0x05000014u);
    CHECK(art.frmPid == 0x05000025u);
}

TEST_CASE("exitGridDirectionForLine gives a vertical line the left or right edge it faces", "[exit_grid]") {
    // A vertical screen segment (dy dominates). A hex left of centre faces LEFT (dir 0).
    const ExitGridArt left = artForLine(0, 200, -300, 0, INTER);
    CHECK(left.proPid == ExitGrid::LEFT_PRO_PID);
    CHECK(left.proPid == 0x05000010u);
    // A hex right of centre faces RIGHT (dir 1).
    const ExitGridArt right = artForLine(0, 200, 300, 0, INTER);
    CHECK(right.proPid == ExitGrid::RIGHT_PRO_PID);
    CHECK(right.proPid == 0x05000011u);
}

TEST_CASE("exitGridDirectionForLine gives a horizontal line the top or bottom edge it faces", "[exit_grid]") {
    const ExitGridArt top = artForLine(200, 0, 0, -300, INTER);
    CHECK(top.proPid == ExitGrid::TOP_PRO_PID);
    CHECK(top.proPid == 0x05000013u);
    const ExitGridArt bottom = artForLine(200, 0, 0, 300, INTER);
    CHECK(bottom.proPid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(bottom.proPid == 0x05000012u);
}

TEST_CASE("exitGridDirectionForLine gives a '\\' line the back-slash diagonal it faces", "[exit_grid]") {
    // A "\" segment runs down-right: dx and dy same sign. Two opposite-facing hexes split the pair.
    const ExitGridArt a = artForLine(120, 120, -200, -200, INTER);
    const ExitGridArt b = artForLine(120, 120, 200, 200, INTER);
    CHECK((a.proPid == ExitGrid::BACK_A_PRO_PID || a.proPid == ExitGrid::BACK_B_PRO_PID));
    CHECK((b.proPid == ExitGrid::BACK_A_PRO_PID || b.proPid == ExitGrid::BACK_B_PRO_PID));
    CHECK(a.proPid != b.proPid); // opposite facings pick opposite sides
}

// The cardinal directions match bhrnddst.map, which uses protos 16-19 (0x05000010..13) for its
// exit-grid rectangle border.
TEST_CASE("The cardinal exit-grid protos are the 0x05000010-13 that bhrnddst.map uses", "[exit_grid]") {
    CHECK(ExitGrid::LEFT_PRO_PID == 0x05000010u);
    CHECK(ExitGrid::RIGHT_PRO_PID == 0x05000011u);
    CHECK(ExitGrid::BOTTOM_PRO_PID == 0x05000012u);
    CHECK(ExitGrid::TOP_PRO_PID == 0x05000013u);
}

TEST_CASE("Every exit-grid proto emitted for a line lies within 0x05000010-17", "[exit_grid]") {
    // Sweep a spread of segments/facings/kinds and assert the proto never escapes the valid range.
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 2; ++dy) {
            checkSegmentProtosInRange(dx, dy);
        }
    }
}

TEST_CASE("A lone exit-grid hex takes the edge it faces outward", "[exit_grid]") {
    // Screen y grows downward: above centre -> TOP, below -> BOTTOM, left -> LEFT, right -> RIGHT.
    CHECK(exitGridArtForFacing(CX, CY - 100, CX, CY, INTER).proPid == ExitGrid::TOP_PRO_PID);
    CHECK(exitGridArtForFacing(CX, CY + 100, CX, CY, INTER).proPid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(exitGridArtForFacing(CX - 100, CY, CX, CY, INTER).proPid == ExitGrid::LEFT_PRO_PID);
    CHECK(exitGridArtForFacing(CX + 100, CY, CX, CY, INTER).proPid == ExitGrid::RIGHT_PRO_PID);
}

TEST_CASE("A lone exit-grid hex takes green or brown art by destination kind", "[exit_grid]") {
    const ExitGridArt green = exitGridArtForFacing(CX, CY - 100, CX, CY, INTER);
    const ExitGridArt brown = exitGridArtForFacing(CX, CY - 100, CX, CY, WORLD);
    CHECK(green.proPid == brown.proPid); // same direction
    CHECK(green.frmPid == ExitGrid::greenFrm(ExitGrid::DIR_TOP));
    CHECK(brown.frmPid == ExitGrid::brownFrm(ExitGrid::DIR_TOP));
}

TEST_CASE("A lone exit-grid hex facing exactly diagonally takes the top or bottom edge", "[exit_grid]") {
    // |dy| >= |dx| classifies as a horizontal line -> top/bottom edge.
    CHECK(exitGridArtForFacing(CX + 100, CY - 100, CX, CY, INTER).proPid == ExitGrid::TOP_PRO_PID);
    CHECK(exitGridArtForFacing(CX - 100, CY + 100, CX, CY, INTER).proPid == ExitGrid::BOTTOM_PRO_PID);
}

TEST_CASE("flipExitGridDirection swaps within each axis pair and is an involution", "[exit_grid]") {
    // The eight directions are four adjacent pairs (0/1, 2/3, 4/5, 6/7); a flip swaps within the pair.
    CHECK(flipExitGridDirection(ExitGrid::DIR_LEFT) == ExitGrid::DIR_RIGHT);
    CHECK(flipExitGridDirection(ExitGrid::DIR_RIGHT) == ExitGrid::DIR_LEFT);
    CHECK(flipExitGridDirection(ExitGrid::DIR_BOTTOM) == ExitGrid::DIR_TOP);
    CHECK(flipExitGridDirection(ExitGrid::DIR_TOP) == ExitGrid::DIR_BOTTOM);
    CHECK(flipExitGridDirection(ExitGrid::DIR_FWD_A) == ExitGrid::DIR_FWD_B);
    CHECK(flipExitGridDirection(ExitGrid::DIR_FWD_B) == ExitGrid::DIR_FWD_A);
    CHECK(flipExitGridDirection(ExitGrid::DIR_BACK_A) == ExitGrid::DIR_BACK_B);
    CHECK(flipExitGridDirection(ExitGrid::DIR_BACK_B) == ExitGrid::DIR_BACK_A);
    // Flipping twice returns the original; out-of-range dirs pass through unchanged.
    for (int dir = 0; dir < ExitGrid::DIR_COUNT; ++dir) {
        CHECK(flipExitGridDirection(flipExitGridDirection(dir)) == dir);
        // A flip keeps the axis (same pair) but always changes the direction.
        CHECK(flipExitGridDirection(dir) != dir);
        CHECK(flipExitGridDirection(dir) / 2 == dir / 2);
    }
    CHECK(flipExitGridDirection(-1) == -1);
    CHECK(flipExitGridDirection(ExitGrid::DIR_COUNT) == ExitGrid::DIR_COUNT);
}

TEST_CASE("exitGridDirectionForLine picks one side for the whole stroke and the flip inverts it", "[exit_grid]") {
    // A horizontal stroke whose midpoint is BELOW centre -> the BOTTOM (south) edge; the flip turns it
    // into the TOP (north) edge. This is the south<->north flip the F key drives.
    const int south = exitGridDirectionForLine(/*dx=*/200, /*dy=*/0,
        /*outwardX=*/0, /*outwardY=*/300, /*flipSide=*/false);
    CHECK(south == ExitGrid::DIR_BOTTOM);
    const int north = exitGridDirectionForLine(200, 0, 0, 300, /*flipSide=*/true);
    CHECK(north == ExitGrid::DIR_TOP);
    CHECK(north == flipExitGridDirection(south));

    // A vertical stroke right of centre -> RIGHT; flipped -> LEFT.
    const int right = exitGridDirectionForLine(0, 200, 300, 0, false);
    const int left = exitGridDirectionForLine(0, 200, 300, 0, true);
    CHECK(right == ExitGrid::DIR_RIGHT);
    CHECK(left == ExitGrid::DIR_LEFT);

    // A "\" stroke (dx, dy same sign): flipped picks the other "\" side, never escaping the pair.
    const int backA = exitGridDirectionForLine(120, 120, -200, -200, false);
    const int backB = exitGridDirectionForLine(120, 120, -200, -200, true);
    CHECK((backA == ExitGrid::DIR_BACK_A || backA == ExitGrid::DIR_BACK_B));
    CHECK(backB == flipExitGridDirection(backA));
    CHECK(backA / 2 == ExitGrid::DIR_BACK_A / 2); // same "\" pair
}

TEST_CASE("exitGridOutward gives each direction's off-map screen vector", "[exit_grid]") {
    // Screen y grows DOWNWARD. This vector is the side a marker's art faces; secondRowNeighbor uses it
    // to pick the diagonal band's second row (the neighbour leaning most along it). A flip (dir ^ 1)
    // reverses the sign, mirroring that facing to the OTHER side of the line. Each cardinal caps its
    // named screen edge.
    CHECK(exitGridOutward(ExitGrid::DIR_LEFT) == std::pair{ -1, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_RIGHT) == std::pair{ 1, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_BOTTOM) == std::pair{ 0, 1 });
    CHECK(exitGridOutward(ExitGrid::DIR_TOP) == std::pair{ 0, -1 });
    // The diagonals face PERPENDICULAR to their band's on-screen line (integer approximations of the
    // measured ~-9° "/" and ~+26° "\" normals). The DIRECTION lives here and must flip sign across a
    // dir ^ 1 pair so the band switches sides.
    CHECK(exitGridOutward(ExitGrid::DIR_FWD_A) == std::pair{ 1, 6 });   // "/" side A: down
    CHECK(exitGridOutward(ExitGrid::DIR_FWD_B) == std::pair{ -1, -6 }); // "/" side B: up (the ^1 flip)
    CHECK(exitGridOutward(ExitGrid::DIR_BACK_A) == std::pair{ -1, 2 }); // "\" side A: down-left
    CHECK(exitGridOutward(ExitGrid::DIR_BACK_B) == std::pair{ 1, -2 }); // "\" side B: up-right (^1 flip)
}

TEST_CASE("exitGridOutward points flip partners opposite ways and an out-of-range direction nowhere", "[exit_grid]") {
    // The two members of each axis pair (the dir ^ 1 partners) are exact negatives, so a flip moves the
    // bar to the opposite side. This holds for BOTH the cardinals and the diagonals now.
    const std::array<std::pair<int, int>, 4> pairs{ {
        { ExitGrid::DIR_LEFT, ExitGrid::DIR_RIGHT },
        { ExitGrid::DIR_BOTTOM, ExitGrid::DIR_TOP },
        { ExitGrid::DIR_FWD_A, ExitGrid::DIR_FWD_B },
        { ExitGrid::DIR_BACK_A, ExitGrid::DIR_BACK_B },
    } };
    for (const auto& [a, b] : pairs) {
        const auto oa = exitGridOutward(a);
        const auto ob = exitGridOutward(b);
        CHECK(oa.first == -ob.first);
        CHECK(oa.second == -ob.second);
        CHECK_FALSE((oa.first == 0 && oa.second == 0)); // every direction has a (sign-flippable) vector
    }
    CHECK(exitGridOutward(-1) == std::pair{ 0, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_COUNT) == std::pair{ 0, 0 });
}

// --------------------------------------------------------------------------------------------------
// ISO-DIAMOND GEOMETRY LOCK
//
// "Draw edge" classifies a stroke by its first->last SCREEN delta. Real sloped exits run along the iso
// diamond's two slanted edges, whose SCREEN slopes are NOT 1:1: measured on the real 200x200 grid, the
// shallow NE/SW edge is ~4:1 and the steep NW/SE edge is ~1.33:1. The cases below feed those EXACT
// measured deltas to the classifier and assert they yield diagonal "/" "\" art (dirs 4..7), locking the
// geometry so the band can't silently regress to a 45°-centred band (which tipped the 4:1 edge to a
// cardinal — the original bug).

namespace {
bool isDiagonalProto(uint32_t proPid) {
    return proPid >= ExitGrid::FWD_A_PRO_PID && proPid <= ExitGrid::BACK_B_PRO_PID; // 0x05000014..17
}
bool isDiagonalDir(int dir) { return dir >= ExitGrid::DIR_FWD_A && dir <= ExitGrid::DIR_BACK_B; }

// position = row * WIDTH + col on the 200x200 grid.
constexpr int hexPos(int col, int row) { return row * HexagonGrid::GRID_WIDTH + col; }
} // namespace

TEST_CASE("classifySegment classes the measured iso-diamond edge deltas as diagonal, not cardinal", "[exit_grid]") {
    using geck::exitgrid_detail::classifySegment;
    using Axis = geck::exitgrid_detail::SegmentAxis;

    // Shallow NE/SW edge, measured delta (-720, 180), slope 4:1 — fell to Horizontal under the old band
    // (the bug); MUST be diagonal.
    CHECK(classifySegment(-720, 180) == Axis::ForwardSlash);
    // Steep NW/SE diamond edge, measured delta (480, 360), slope ~1.33:1.
    CHECK(classifySegment(480, 360) == Axis::BackSlash);

    // Near-pure screen axes still resolve to the cardinal arts (these are the rect-tool edges).
    CHECK(classifySegment(800, 0) == Axis::Horizontal); // pure horizontal screen drag
    CHECK(classifySegment(0, 600) == Axis::Vertical);   // pure vertical screen drag
}

TEST_CASE("exitGridDirectionForLine turns the measured iso-diamond edge deltas into diagonal directions", "[exit_grid]") {
    // Shallow 4:1 edge: feed a representative outward facing; the result must be one of the "/" "\"
    // dirs (4..7), never a cardinal. (The side within the pair depends on outward facing, which we
    // only need to be diagonal here.)
    const int shallow = exitGridDirectionForLine(/*dx=*/-720, /*dy=*/180,
        /*outwardX=*/-300, /*outwardY=*/-200, /*flipSide=*/false);
    CHECK(isDiagonalDir(shallow));
    const int steep = exitGridDirectionForLine(/*dx=*/480, /*dy=*/360,
        /*outwardX=*/300, /*outwardY=*/200, /*flipSide=*/false);
    CHECK(isDiagonalDir(steep));

    // Cardinal deltas stay cardinal: a near-horizontal stroke -> TOP/BOTTOM, near-vertical -> LEFT/RIGHT.
    CHECK(exitGridDirectionForLine(800, 0, 0, 300, false) == ExitGrid::DIR_BOTTOM);
    CHECK(exitGridDirectionForLine(800, 0, 0, -300, false) == ExitGrid::DIR_TOP);
    CHECK(exitGridDirectionForLine(0, 600, 300, 0, false) == ExitGrid::DIR_RIGHT);
    CHECK(exitGridDirectionForLine(0, 600, -300, 0, false) == ExitGrid::DIR_LEFT);
}

TEST_CASE("exitGridArtForStroke commits a diagonal proto for a stroke along the iso-diamond edge", "[exit_grid]") {
    HexagonGrid grid;
    constexpr int C = 100; // a central col/row, well inside the diamond

    // Shallow NE/SW edge: vary the column only (a const-row hex run). Screen slope ~4:1.
    const ExitGridArt shallow = exitGridArtForStroke(&grid, geck::hexline::hexLine(hexPos(C, C), hexPos(C + 30, C)),
        ExitGridDestinationKind::InterMap, false);
    CHECK(isDiagonalProto(shallow.proPid));
    // green frm = diagonal proto + 1
    CHECK(shallow.frmPid == shallow.proPid + 1);

    // Steep NW/SE edge: vary the row only (a const-col hex run). Screen slope ~1.33:1.
    const ExitGridArt steep = exitGridArtForStroke(&grid, geck::hexline::hexLine(hexPos(C, C), hexPos(C, C + 30)),
        ExitGridDestinationKind::WorldMap, false);
    CHECK(isDiagonalProto(steep.proPid));
    // brown frm = diagonal proto + 0x11
    CHECK(steep.frmPid == steep.proPid + 0x11);
}

TEST_CASE("exitGridArtForStroke's flip inverts the whole stroke's side", "[exit_grid]") {
    HexagonGrid grid;
    const std::vector<int> stroke = geck::hexline::hexLine(hexPos(100, 100), hexPos(100, 130));
    const ExitGridArt plain = exitGridArtForStroke(&grid, stroke, INTER, false);
    const ExitGridArt flipped = exitGridArtForStroke(&grid, stroke, INTER, true);
    CHECK(exitGridDirOfProto(flipped.proPid) == flipExitGridDirection(exitGridDirOfProto(plain.proPid)));
}

TEST_CASE("exitGridArtForStroke classifies a lone hex by its facing and still honours the flip", "[exit_grid]") {
    HexagonGrid grid;
    // A hex near the top row sits above the grid centre on screen -> the TOP edge.
    const std::vector<int> lone{ hexPos(100, 5) };
    const auto [hx, hy] = geck::hexScreenPosition(grid, lone.front());
    const auto [cx, cy] = geck::hexGridCenterScreen(grid);
    const ExitGridArt facing = exitGridArtForFacing(hx, hy, cx, cy, WORLD);
    CHECK(exitGridArtForStroke(&grid, lone, WORLD, false).proPid == facing.proPid);
    CHECK(exitGridArtForStroke(&grid, lone, WORLD, false).frmPid == facing.frmPid);
    const ExitGridArt flipped = exitGridArtForStroke(&grid, lone, WORLD, true);
    CHECK(exitGridDirOfProto(flipped.proPid) == flipExitGridDirection(exitGridDirOfProto(facing.proPid)));
    CHECK(flipped.frmPid == ExitGrid::brownFrm(exitGridDirOfProto(flipped.proPid)));
}

TEST_CASE("exitGridArtForStroke falls back to a bottom-edge marker without a grid or with an empty stroke", "[exit_grid]") {
    HexagonGrid grid;
    const ExitGridArt noGrid = exitGridArtForStroke(nullptr, { hexPos(100, 100) }, WORLD, false);
    CHECK(noGrid.proPid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(noGrid.frmPid == ExitGrid::brownFrm(ExitGrid::DIR_BOTTOM));
    const ExitGridArt empty = exitGridArtForStroke(&grid, {}, INTER, true);
    CHECK(empty.proPid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(empty.frmPid == ExitGrid::greenFrm(ExitGrid::DIR_BOTTOM));
}

// --------------------------------------------------------------------------------------------------
// TRUE-FREEZE per-segment capture. Each polyline segment is classified once from its own screen
// direction, captured with the flip at the click that closed it, and never recomputed;
// flattenExitGridSegments must keep a committed segment independent of any later flip/cursor change.

namespace {
// A segment frozen with the flip in effect right now: every hex carries the run's production art.
ExitGridSegment captureSegment(const ExitGridSegmentRun& run, bool flipAtCapture) {
    return { run.hexes, std::vector<ExitGridArt>(run.hexes.size(), exitGridArtForRun(run, INTER, flipAtCapture)) };
}

std::vector<int> dirsOf(const std::vector<ExitGridArt>& art) {
    std::vector<int> dirs;
    for (const ExitGridArt& a : art) {
        dirs.push_back(exitGridDirOfProto(a.proPid));
    }
    return dirs;
}
} // namespace

TEST_CASE("A captured exit-grid segment keeps its direction when a later segment flips", "[exit_grid]") {
    // A horizontal segment captured with NO flip -> BOTTOM. The captured value is a snapshot: changing
    // the flip on a LATER (live) segment can never alter it.
    ExitGridSegmentRun run;
    run.hexes = { 10, 11, 12 };
    run.screenDx = 200;
    run.screenDy = 0;
    run.outwardX = 0;
    run.outwardY = 300; // below centre -> BOTTOM without flip

    const ExitGridSegment frozen = captureSegment(run, /*flipAtCapture=*/false);
    CHECK(exitGridDirOfProto(frozen.art.front().proPid) == ExitGrid::DIR_BOTTOM);

    // The user now presses Space (the live flip becomes true) and moves the cursor. Re-flattening with
    // any live segment and any current flip leaves the committed segment's hexes/dirs untouched.
    for (const bool liveFlip : { false, true }) {
        ExitGridSegmentRun liveRun;
        liveRun.hexes = { 12, 213, 414 }; // shares hex 12 with the frozen segment
        liveRun.screenDx = 480;
        liveRun.screenDy = 360; // a "\" diagonal
        liveRun.outwardX = 300;
        liveRun.outwardY = 200;
        const ExitGridSegment live = captureSegment(liveRun, liveFlip);

        std::vector<int> hexes;
        std::vector<ExitGridArt> art;
        flattenExitGridSegments({ frozen }, live, hexes, art);
        const std::vector<int> dirs = dirsOf(art);

        // The frozen segment's hexes come first and keep BOTTOM regardless of the live flip; hex 12 is
        // shared and first-seen (frozen) wins, so it stays BOTTOM too.
        REQUIRE(hexes.size() == 5);
        CHECK(hexes == std::vector<int>{ 10, 11, 12, 213, 414 });
        CHECK(dirs[0] == ExitGrid::DIR_BOTTOM); // hex 10 (frozen)
        CHECK(dirs[1] == ExitGrid::DIR_BOTTOM); // hex 11 (frozen)
        CHECK(dirs[2] == ExitGrid::DIR_BOTTOM); // hex 12 (shared, frozen wins)
        // The live segment is the only thing the flip touches: it's a "\" diagonal, flipped or not.
        CHECK(isDiagonalDir(dirs[3]));
        CHECK(dirs[4] == dirs[3]);
        if (liveFlip) {
            CHECK(dirs[3] == flipExitGridDirection(dirsOf(captureSegment(liveRun, false).art).front()));
        }
    }
}

TEST_CASE("Committed exit-grid segments stay put as the live segment moves", "[exit_grid]") {
    // Two committed segments captured at different flips; as the live segment sweeps several cursor
    // positions the committed hexes/dirs must stay byte-identical. The "committed segments stay put"
    // guarantee.
    ExitGridSegmentRun r1;
    r1.hexes = { 1, 2, 3 };
    r1.screenDx = 200;
    r1.screenDy = 0;
    r1.outwardX = 0;
    r1.outwardY = 300; // BOTTOM
    const ExitGridSegment c1 = captureSegment(r1, /*flipAtCapture=*/false);

    ExitGridSegmentRun r2;
    r2.hexes = { 3, 4, 5 }; // shares hex 3 with c1
    r2.screenDx = 0;
    r2.screenDy = 200; // vertical -> LEFT/RIGHT
    r2.outwardX = 300;
    r2.outwardY = 0;
    const ExitGridSegment c2 = captureSegment(r2, /*flipAtCapture=*/true); // captured with flip ON

    std::vector<int> baselineHexes;
    std::vector<ExitGridArt> baselineArt;
    flattenExitGridSegments({ c1, c2 }, ExitGridSegment{}, baselineHexes, baselineArt);
    const std::vector<int> baselineDirs = dirsOf(baselineArt);
    // The shared hex 3 keeps c1's BOTTOM (first-seen); c2 contributes 4,5 at its captured (flipped) side.
    REQUIRE(baselineHexes == std::vector<int>{ 1, 2, 3, 4, 5 });
    CHECK(baselineDirs[2] == ExitGrid::DIR_BOTTOM);                       // shared hex 3, c1 wins
    CHECK(baselineDirs[3] == flipExitGridDirection(ExitGrid::DIR_RIGHT)); // c2 captured flipped -> LEFT

    // Sweep the live segment; the committed prefix never moves.
    for (int sweep = 50; sweep <= 400; sweep += 50) {
        ExitGridSegmentRun liveRun;
        liveRun.hexes = { 6, 7 };
        liveRun.screenDx = sweep;
        liveRun.screenDy = 0;
        liveRun.outwardX = 0;
        liveRun.outwardY = -300;
        std::vector<int> hexes;
        std::vector<ExitGridArt> art;
        flattenExitGridSegments({ c1, c2 }, captureSegment(liveRun, /*flipAtCapture=*/true), hexes, art);
        const std::vector<int> dirs = dirsOf(art);

        // The committed prefix (5 hexes) is identical to the baseline; only the live tail (6,7) is added.
        REQUIRE(hexes.size() == 7);
        for (std::size_t i = 0; i < baselineHexes.size(); ++i) {
            CHECK(hexes[i] == baselineHexes[i]);
            CHECK(dirs[i] == baselineDirs[i]);
        }
    }
}

// --------------------------------------------------------------------------------------------------
// SHIFT-SNAP: a pure (lastVertex, cursor) -> snapped cursor function. The eight clean angles are the
// exit-grid edge SCREEN directions (horizontal, vertical, and the two iso diagonals at the measured
// 4:1 / 4:3 slopes, both signs). Snapping keeps the distance and rotates the angle to the nearest one.

namespace {
double distance(double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    return std::sqrt(dx * dx + dy * dy);
}
} // namespace

TEST_CASE("exitGridSnapDirections gives eight unit vectors at the measured iso slopes", "[exit_grid]") {
    const auto& dirs = exitGridSnapDirections();
    REQUIRE(dirs.size() == 8);
    for (const auto& d : dirs) {
        CHECK(std::abs(std::sqrt(d.x * d.x + d.y * d.y) - 1.0) < 1e-9); // unit length
    }
    // The shallow iso edge has screen slope |dx/dy| == 4 (e.g. measured delta (-720, 180)).
    bool sawShallow = false;
    bool sawSteep = false;
    for (const auto& d : dirs) {
        if (d.y != 0.0 && d.x != 0.0) {
            const double slope = std::abs(d.x / d.y);
            if (std::abs(slope - 4.0) < 1e-6) {
                sawShallow = true; // 4:1
            }
            if (std::abs(slope - 4.0 / 3.0) < 1e-6) {
                sawSteep = true; // 4:3
            }
        }
    }
    CHECK(sawShallow);
    CHECK(sawSteep);
}

TEST_CASE("snapToExitGridAngle snaps a cursor a few degrees off a clean angle onto it, keeping the distance", "[exit_grid]") {
    constexpr double last_x = 1000.0;
    constexpr double last_y = 1000.0;
    const auto& dirs = exitGridSnapDirections();

    for (const auto& d : dirs) {
        constexpr double len = 250.0;
        // A cursor exactly on the ray at distance `len`.
        const double onX = last_x + d.x * len;
        const double onY = last_y + d.y * len;
        // Rotate it a few degrees off the ray (about the last vertex), keeping the distance.
        constexpr double theta = 5.0 * 3.14159265358979323846 / 180.0;
        const double ox = onX - last_x;
        const double oy = onY - last_y;
        const double offX = last_x + ox * std::cos(theta) - oy * std::sin(theta);
        const double offY = last_y + ox * std::sin(theta) + oy * std::cos(theta);

        const auto [sx, sy] = snapToExitGridAngle(last_x, last_y, offX, offY);
        // Snaps back onto the clean ray...
        CHECK(std::abs(sx - onX) < 1e-6);
        CHECK(std::abs(sy - onY) < 1e-6);
        // ...preserving the distance from the last vertex.
        CHECK(std::abs(distance(last_x, last_y, sx, sy) - len) < 1e-6);
    }
}

TEST_CASE("snapToExitGridAngle returns a zero-length offset unchanged", "[exit_grid]") {
    const auto [sx, sy] = snapToExitGridAngle(500.0, 500.0, 500.0, 500.0);
    CHECK(sx == 500.0);
    CHECK(sy == 500.0);
}

// --- Diagonal direction helpers --------------------------------------------------------------------

TEST_CASE("Only the four slash directions are diagonal, and every exit-grid proto maps back to its direction", "[exit_grid]") {
    CHECK_FALSE(isDiagonalExitGridDir(ExitGrid::DIR_LEFT));
    CHECK_FALSE(isDiagonalExitGridDir(ExitGrid::DIR_BOTTOM));
    CHECK(isDiagonalExitGridDir(ExitGrid::DIR_FWD_A));
    CHECK(isDiagonalExitGridDir(ExitGrid::DIR_BACK_B));
    for (int dir = 0; dir < ExitGrid::DIR_COUNT; ++dir) {
        CHECK(exitGridDirOfProto(ExitGrid::exitGridProto(dir)) == dir);
    }
}

// --- Second-row neighbour (the 2nd parallel row of REAL diagonal markers) --------------------------

TEST_CASE("secondRowNeighbor finds no second row for a cardinal direction", "[exit_grid]") {
    // A spread of neighbours so a wrong "max dot" pick would surface; cardinals must still bail with -1.
    const std::vector<NeighborOffset> neighbors = {
        { 10, 1, 0 }, { 11, -1, 0 }, { 12, 0, 1 }, { 13, 0, -1 }
    };
    CHECK(secondRowNeighbor(ExitGrid::DIR_LEFT, neighbors) == -1);
    CHECK(secondRowNeighbor(ExitGrid::DIR_RIGHT, neighbors) == -1);
    CHECK(secondRowNeighbor(ExitGrid::DIR_BOTTOM, neighbors) == -1);
    CHECK(secondRowNeighbor(ExitGrid::DIR_TOP, neighbors) == -1);
}

TEST_CASE("secondRowNeighbor picks the neighbour leaning most along a diagonal's outward normal", "[exit_grid]") {
    // outward(FWD_A) = (1, 6): the (0, +1) downward neighbour has dot 6, the largest here.
    const std::vector<NeighborOffset> n = {
        { 20, 1, 0 },  // dot vs (1,6) = 1
        { 21, 0, 1 },  // dot = 6  <- winner for FWD_A
        { 22, 0, -1 }, // dot = -6 (skipped, negative)
        { 23, -1, 0 }, // dot = -1
    };
    CHECK(secondRowNeighbor(ExitGrid::DIR_FWD_A, n) == 21);
    // FWD_B = (-1, -6): the upward neighbour (0,-1) now wins (the flip mirrors to the other side).
    CHECK(secondRowNeighbor(ExitGrid::DIR_FWD_B, n) == 22);

    // outward(BACK_A) = (-1, 2): (0,+1) gives dot 2, (-1,0) gives dot 1; the downward neighbour wins.
    CHECK(secondRowNeighbor(ExitGrid::DIR_BACK_A, n) == 21);
    // BACK_B = (1, -2): the flip -> (0,-1) gives dot 2, the up neighbour wins.
    CHECK(secondRowNeighbor(ExitGrid::DIR_BACK_B, n) == 22);
}

TEST_CASE("secondRowNeighbor mirrors its pick to the opposite side for a flipped diagonal", "[exit_grid]") {
    // Symmetric pair of neighbours straddling the band; flipping dir must swap which one is chosen.
    const std::vector<NeighborOffset> n = {
        { 30, 1, 5 },   // leans the FWD_A (down) way
        { 31, -1, -5 }, // leans the FWD_B (up) way
    };
    const int a = secondRowNeighbor(ExitGrid::DIR_FWD_A, n);
    const int b = secondRowNeighbor(flipExitGridDirection(ExitGrid::DIR_FWD_A), n);
    CHECK(a == 30);
    CHECK(b == 31);
    CHECK(a != b);
}

TEST_CASE("secondRowNeighbor never picks a neighbour perpendicular to the outward normal", "[exit_grid]") {
    // outward(FWD_A) = (1, 6). The offset (6, -1) is perpendicular: dot = 6*1 + (-1)*6 = 0. With the
    // bestDot seed at 0 and a strict dot > bestDot test, a sole perpendicular neighbour yields -1.
    const std::vector<NeighborOffset> perpOnly = { { 40, 6, -1 } };
    CHECK(secondRowNeighbor(ExitGrid::DIR_FWD_A, perpOnly) == -1);

    // Adding one strictly-outward neighbour, the perpendicular one is still passed over.
    const std::vector<NeighborOffset> perpPlus = { { 40, 6, -1 }, { 41, 0, 1 } };
    CHECK(secondRowNeighbor(ExitGrid::DIR_FWD_A, perpPlus) == 41);

    // No neighbour leans outward (all dot <= 0) -> -1.
    const std::vector<NeighborOffset> none = { { 50, 6, -1 }, { 51, -1, 0 } };
    CHECK(secondRowNeighbor(ExitGrid::DIR_FWD_A, none) == -1);
}

// --- Placement: a diagonal segment grows a SECOND row of REAL hexes, a cardinal stays single-row -----

TEST_CASE("A diagonal exit-grid band places about twice the line's hexes and a cardinal one just the line", "[exit_grid]") {
    HexagonGrid grid;
    constexpr int C = 100;

    // A diagonal "\" run (vary the row -> the steep iso edge classifies BackSlash, dir 6/7).
    const int dStart = hexPos(C, C);
    const int dEnd = hexPos(C, C + 30);
    const std::vector<int> line = geck::hexline::hexLine(dStart, dEnd);
    const std::size_t lineLen = line.size();
    REQUIRE(lineLen >= 2);

    const std::vector<int> diag = exitGridBandHexes(grid, line, ExitGrid::DIR_BACK_A);
    // Two rows of REAL hexes: nearly double the drawn line (a few endpoints can collide and dedup).
    REQUIRE(diag.size() > lineLen);
    CHECK(diag.size() >= lineLen + lineLen - 2);
    CHECK(diag.size() <= 2 * lineLen);
    // Every hex is unique (the dedup held) and the original line is fully contained.
    CHECK(std::set<int>(diag.begin(), diag.end()).size() == diag.size());
    CHECK(std::vector<int>(diag.begin(), diag.begin() + static_cast<std::ptrdiff_t>(lineLen)) == line);
    // The second row sits EXIT_GRID_SECOND_ROW_STEPS hexes over, on the side the BACK_A bars face.
    const int second = geck::exitGridSecondRowHex(grid, line.front(), ExitGrid::DIR_BACK_A, geck::EXIT_GRID_SECOND_ROW_STEPS);
    REQUIRE(second >= 0);
    CHECK(std::find(diag.begin(), diag.end(), second) != diag.end());
    CHECK(geck::hexgrid::distance(line.front(), second) == geck::EXIT_GRID_SECOND_ROW_STEPS);
    const auto [fx, fy] = geck::hexScreenPosition(grid, line.front());
    const auto [sx, sy] = geck::hexScreenPosition(grid, second);
    const auto [ox, oy] = exitGridOutward(ExitGrid::DIR_BACK_A);
    CHECK((sx - fx) * ox + (sy - fy) * oy > 0);

    // A cardinal run (pure horizontal hex row -> LEFT/RIGHT) stays single-row.
    const std::vector<int> cardLine = geck::hexline::hexLine(hexPos(C, C), hexPos(C + 20, C));
    CHECK(exitGridBandHexes(grid, cardLine, ExitGrid::DIR_RIGHT) == cardLine);
}

TEST_CASE("A flipped diagonal exit-grid band grows its second row on the opposite side", "[exit_grid]") {
    HexagonGrid grid;
    constexpr int C = 100;
    const std::vector<int> line = geck::hexline::hexLine(hexPos(C, C), hexPos(C, C + 30));
    REQUIRE(line.size() >= 2);

    const int second = geck::exitGridSecondRowHex(grid, line.front(), ExitGrid::DIR_BACK_B, geck::EXIT_GRID_SECOND_ROW_STEPS);
    REQUIRE(second >= 0);
    const std::vector<int> band = exitGridBandHexes(grid, line, ExitGrid::DIR_BACK_B);
    CHECK(std::find(band.begin(), band.end(), second) != band.end());

    // The row leans the way the BACK_B bars face, which is away from where BACK_A's would.
    const auto [fx, fy] = geck::hexScreenPosition(grid, line.front());
    const auto [sx, sy] = geck::hexScreenPosition(grid, second);
    const auto [bx, by] = exitGridOutward(ExitGrid::DIR_BACK_B);
    const auto [ax, ay] = exitGridOutward(ExitGrid::DIR_BACK_A);
    CHECK((sx - fx) * bx + (sy - fy) * by > 0);
    CHECK((sx - fx) * ax + (sy - fy) * ay < 0);
}

TEST_CASE("exitGridSecondRowHex gives -1 for an off-grid start or a step off the grid", "[exit_grid]") {
    HexagonGrid grid;
    const int pastEnd = HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT;
    CHECK(geck::exitGridSecondRowHex(grid, -1, ExitGrid::DIR_BACK_A, geck::EXIT_GRID_SECOND_ROW_STEPS) == -1);
    CHECK(geck::exitGridSecondRowHex(grid, pastEnd, ExitGrid::DIR_BACK_A, geck::EXIT_GRID_SECOND_ROW_STEPS) == -1);

    // Corner hex 0: BACK_B has no neighbour leaning its way at all; FWD_B's first step stays on the grid
    // and its second walks off. FWD_A leans inward and finds a real hex.
    CHECK(geck::exitGridSecondRowHex(grid, 0, ExitGrid::DIR_BACK_B, 1) == -1);
    CHECK(geck::exitGridSecondRowHex(grid, 0, ExitGrid::DIR_FWD_B, 1) >= 0);
    CHECK(geck::exitGridSecondRowHex(grid, 0, ExitGrid::DIR_FWD_B, 2) == -1);
    CHECK(geck::exitGridSecondRowHex(grid, 0, ExitGrid::DIR_FWD_A, 2) >= 0);
}

TEST_CASE("A diagonal exit-grid band along the grid edge adds no off-grid second row", "[exit_grid]") {
    HexagonGrid grid;
    // Row 0 is the grid's top edge: FWD_B's second row would lie off the grid, FWD_A's lies inside.
    const std::vector<int> edge = geck::hexline::hexLine(hexPos(40, 0), hexPos(60, 0));
    REQUIRE(edge.size() >= 2);
    CHECK(exitGridBandHexes(grid, edge, ExitGrid::DIR_FWD_B) == edge);
    const std::vector<int> inward = exitGridBandHexes(grid, edge, ExitGrid::DIR_FWD_A);
    CHECK(inward.size() > edge.size());
    for (const int hex : inward) {
        CHECK(grid.getHexByPosition(static_cast<uint32_t>(hex)).has_value());
    }
}

TEST_CASE("exitGridSegmentRun takes the axis from the ends and the side from the middle hex", "[exit_grid]") {
    HexagonGrid grid;
    constexpr int C = 100;
    // Four hexes eastward along one row: the "middle" of an even run is hexes[size / 2], the third.
    const std::vector<int> hexes{ hexPos(C, C), hexPos(C + 1, C), hexPos(C + 2, C), hexPos(C + 3, C) };
    const ExitGridSegmentRun run = geck::exitGridSegmentRun(grid, hexes);

    const auto [firstX, firstY] = geck::hexScreenPosition(grid, hexes.front());
    const auto [lastX, lastY] = geck::hexScreenPosition(grid, hexes.back());
    const auto [midX, midY] = geck::hexScreenPosition(grid, hexes[2]);
    const auto [centerX, centerY] = geck::hexGridCenterScreen(grid);
    CHECK(run.hexes == hexes);
    CHECK(run.screenDx == lastX - firstX);
    CHECK(run.screenDy == lastY - firstY);
    CHECK(run.outwardX == midX - centerX);
    CHECK(run.outwardY == midY - centerY);
    // Hex columns grow leftward on screen, and hex (C, C) is the grid centre.
    CHECK(run.screenDx < 0);
    CHECK(run.outwardX < 0);
    const int secondX = geck::hexScreenPosition(grid, hexes[1]).first;
    CHECK(run.outwardX != secondX - centerX); // not the lower middle

    // Reversing the run flips the axis sign; a single hex has no extent.
    const ExitGridSegmentRun reversed = geck::exitGridSegmentRun(grid, { hexes.rbegin(), hexes.rend() });
    CHECK(reversed.screenDx == -run.screenDx);
    CHECK(reversed.screenDy == -run.screenDy);
    const ExitGridSegmentRun lone = geck::exitGridSegmentRun(grid, { hexPos(C, C) });
    CHECK(lone.screenDx == 0);
    CHECK(lone.screenDy == 0);
    CHECK(lone.outwardX == 0);
    CHECK(lone.outwardY == 0);
}
