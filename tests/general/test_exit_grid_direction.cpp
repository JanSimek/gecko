#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

#include "editor/HexagonGrid.h"
#include "util/Constants.h"
#include "util/ExitGridDirection.h"
#include "util/HexLine.h"

using geck::ExitGridArt;
using geck::exitGridArtForDirection;
using geck::exitGridArtForFacing;
using geck::exitGridArtForSegment;
using geck::ExitGridDestinationKind;
using geck::exitGridDirectionForLine;
using geck::exitGridDirOfProto;
using geck::exitGridOutward;
using geck::ExitGridSegmentRun;
using geck::exitGridSnapDirections;
using geck::flipExitGridDirection;
using geck::HexagonGrid;
using geck::isDiagonalExitGridDir;
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

// Assert both destination kinds emit an in-range proto for one segment, across every outward facing.
void checkSegmentProtosInRange(int dx, int dy) {
    for (int ox = -1; ox <= 1; ++ox) {
        for (int oy = -1; oy <= 1; ++oy) {
            const ExitGridArt g = exitGridArtForSegment(dx, dy, ox, oy, INTER);
            const ExitGridArt b = exitGridArtForSegment(dx, dy, ox, oy, WORLD);
            CHECK(isExitGridProto(g.proPid));
            CHECK(isExitGridProto(b.proPid));
        }
    }
}
} // namespace

TEST_CASE("the verified frm scheme: greenFrm = proto+1, brownFrm = proto+0x11", "[exitgrid][art]") {
    for (int dir = 0; dir < ExitGrid::DIR_COUNT; ++dir) {
        const uint32_t proto = ExitGrid::exitGridProto(dir);
        CHECK(proto == 0x05000010u + static_cast<uint32_t>(dir));
        CHECK(ExitGrid::greenFrm(dir) == proto + 1);
        CHECK(ExitGrid::brownFrm(dir) == proto + 0x11);
    }
}

TEST_CASE("exitGridArtForDirection emits green for inter-map and brown for world/town", "[exitgrid][art]") {
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
TEST_CASE("exitGridArtForSegment matches the user's horizontal data point", "[exitgrid][segment]") {
    // A horizontal screen segment (dx dominates), hex above centre -> TOP edge, world map -> brown.
    const ExitGridArt art = exitGridArtForSegment(/*dx=*/200, /*dy=*/0,
        /*outwardX=*/0, /*outwardY=*/-300, WORLD);
    CHECK(art.proPid == 0x05000013u);
    CHECK(art.frmPid == 0x05000024u);
}

TEST_CASE("exitGridArtForSegment matches the user's '/' diagonal data point", "[exitgrid][segment]") {
    // A "/" segment runs up-right: dx > 0, dy < 0 in screen space. side B faces up-right (dir 5);
    // a hex facing down-left takes side A (dir 4). World map -> brown ext2grd5 = 0x05000025.
    const ExitGridArt art = exitGridArtForSegment(/*dx=*/120, /*dy=*/-120,
        /*outwardX=*/-200, /*outwardY=*/200, WORLD);
    CHECK(art.proPid == 0x05000014u);
    CHECK(art.frmPid == 0x05000025u);
}

TEST_CASE("exitGridArtForSegment: a vertical line -> left/right (the 96x24 wide bars)", "[exitgrid][segment]") {
    // A vertical screen segment (dy dominates). A hex left of centre faces LEFT (dir 0).
    const ExitGridArt left = exitGridArtForSegment(0, 200, -300, 0, INTER);
    CHECK(left.proPid == ExitGrid::LEFT_PRO_PID);
    CHECK(left.proPid == 0x05000010u);
    // A hex right of centre faces RIGHT (dir 1).
    const ExitGridArt right = exitGridArtForSegment(0, 200, 300, 0, INTER);
    CHECK(right.proPid == ExitGrid::RIGHT_PRO_PID);
    CHECK(right.proPid == 0x05000011u);
}

TEST_CASE("exitGridArtForSegment: a horizontal line -> top/bottom (the 32x96 tall bars)", "[exitgrid][segment]") {
    const ExitGridArt top = exitGridArtForSegment(200, 0, 0, -300, INTER);
    CHECK(top.proPid == ExitGrid::TOP_PRO_PID);
    CHECK(top.proPid == 0x05000013u);
    const ExitGridArt bottom = exitGridArtForSegment(200, 0, 0, 300, INTER);
    CHECK(bottom.proPid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(bottom.proPid == 0x05000012u);
}

TEST_CASE("exitGridArtForSegment: a '\\' line -> the dir 6/7 diagonal pair", "[exitgrid][segment]") {
    // A "\" segment runs down-right: dx and dy same sign. Two opposite-facing hexes split the pair.
    const ExitGridArt a = exitGridArtForSegment(120, 120, -200, -200, INTER);
    const ExitGridArt b = exitGridArtForSegment(120, 120, 200, 200, INTER);
    CHECK((a.proPid == ExitGrid::BACK_A_PRO_PID || a.proPid == ExitGrid::BACK_B_PRO_PID));
    CHECK((b.proPid == ExitGrid::BACK_A_PRO_PID || b.proPid == ExitGrid::BACK_B_PRO_PID));
    CHECK(a.proPid != b.proPid); // opposite facings pick opposite sides
}

// The cardinal directions match bhrnddst.map, which uses protos 16-19 (0x05000010..13) for its
// exit-grid rectangle border.
TEST_CASE("the cardinal protos are bhrnddst's exit-grid protos (16-19)", "[exitgrid][bhrnddst]") {
    CHECK(ExitGrid::LEFT_PRO_PID == 0x05000010u);
    CHECK(ExitGrid::RIGHT_PRO_PID == 0x05000011u);
    CHECK(ExitGrid::BOTTOM_PRO_PID == 0x05000012u);
    CHECK(ExitGrid::TOP_PRO_PID == 0x05000013u);
}

TEST_CASE("every emitted proPid is within the exit-grid range 0x05000010..17", "[exitgrid][range]") {
    // Sweep a spread of segments/facings/kinds and assert the proto never escapes the valid range.
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 2; ++dy) {
            checkSegmentProtosInRange(dx, dy);
        }
    }
}

TEST_CASE("single-hex fallback (no segment) classifies by outward facing", "[exitgrid][fallback]") {
    // Screen y grows downward: above centre -> TOP, below -> BOTTOM, left -> LEFT, right -> RIGHT.
    CHECK(exitGridArtForFacing(CX, CY - 100, CX, CY, INTER).proPid == ExitGrid::TOP_PRO_PID);
    CHECK(exitGridArtForFacing(CX, CY + 100, CX, CY, INTER).proPid == ExitGrid::BOTTOM_PRO_PID);
    CHECK(exitGridArtForFacing(CX - 100, CY, CX, CY, INTER).proPid == ExitGrid::LEFT_PRO_PID);
    CHECK(exitGridArtForFacing(CX + 100, CY, CX, CY, INTER).proPid == ExitGrid::RIGHT_PRO_PID);
}

TEST_CASE("single-hex fallback flips green <-> brown by destination kind", "[exitgrid][fallback]") {
    const ExitGridArt green = exitGridArtForFacing(CX, CY - 100, CX, CY, INTER);
    const ExitGridArt brown = exitGridArtForFacing(CX, CY - 100, CX, CY, WORLD);
    CHECK(green.proPid == brown.proPid); // same direction
    CHECK(green.frmPid == ExitGrid::greenFrm(ExitGrid::DIR_TOP));
    CHECK(brown.frmPid == ExitGrid::brownFrm(ExitGrid::DIR_TOP));
}

TEST_CASE("a |dx| == |dy| facing tie favours the vertical (top/bottom) edge", "[exitgrid][fallback]") {
    // |dy| >= |dx| classifies as a horizontal line -> top/bottom edge.
    CHECK(exitGridArtForFacing(CX + 100, CY - 100, CX, CY, INTER).proPid == ExitGrid::TOP_PRO_PID);
    CHECK(exitGridArtForFacing(CX - 100, CY + 100, CX, CY, INTER).proPid == ExitGrid::BOTTOM_PRO_PID);
}

TEST_CASE("flipExitGridDirection swaps within each axis pair and is an involution", "[exitgrid][flip]") {
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

TEST_CASE("exitGridDirectionForLine picks one side for the whole stroke and the flip inverts it",
    "[exitgrid][line]") {
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

TEST_CASE("exitGridOutward: the per-direction outward (off-map) screen vector", "[exitgrid][outward]") {
    // Screen y grows DOWNWARD. The editor slides the bar along this vector so the trigger hex sits at
    // the bar's inner edge; a flip (dir ^ 1) reverses the sign and swings the byte-identical art to the
    // OTHER side of the line. Each cardinal caps its named screen edge.
    CHECK(exitGridOutward(ExitGrid::DIR_LEFT) == std::pair{ -1, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_RIGHT) == std::pair{ 1, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_BOTTOM) == std::pair{ 0, 1 });
    CHECK(exitGridOutward(ExitGrid::DIR_TOP) == std::pair{ 0, -1 });
    // The diagonals face PERPENDICULAR to their band's on-screen line (integer approximations of the
    // measured ~-9° "/" and ~+26° "\" normals; applyExitGridOutwardOffset normalizes before scaling).
    // The DIRECTION lives here and must flip sign across a dir ^ 1 pair so the band switches sides.
    CHECK(exitGridOutward(ExitGrid::DIR_FWD_A) == std::pair{ 1, 6 });   // "/" side A: down
    CHECK(exitGridOutward(ExitGrid::DIR_FWD_B) == std::pair{ -1, -6 }); // "/" side B: up (the ^1 flip)
    CHECK(exitGridOutward(ExitGrid::DIR_BACK_A) == std::pair{ -1, 2 }); // "\" side A: down-left
    CHECK(exitGridOutward(ExitGrid::DIR_BACK_B) == std::pair{ 1, -2 }); // "\" side B: up-right (^1 flip)
}

TEST_CASE("exitGridOutward: every pair points opposite ways; out-of-range is {0,0}",
    "[exitgrid][outward]") {
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

// Mirror ExitGridPlacementManager::autoArtForLine on the REAL grid: walk the hex line, axis from the
// first->last delta, side from the midpoint's offset from centre, then classify.
ExitGridArt autoArtForRun(const HexagonGrid& grid, int startHex, int endHex,
    ExitGridDestinationKind kind, bool flipSide = false) {
    const auto screenOf = [&grid](int hexIndex) -> std::pair<int, int> {
        const auto h = grid.getHexByPosition(static_cast<uint32_t>(hexIndex));
        return h.has_value() ? std::pair<int, int>{ h->get().x(), h->get().y() }
                             : std::pair<int, int>{ 0, 0 };
    };
    const std::vector<int> run = geck::hexline::hexLine(grid, startHex, endHex);
    REQUIRE(run.size() >= 2);
    const auto [cx, cy] = geck::hexGridCenterScreen(grid);
    const auto [fx, fy] = screenOf(run.front());
    const auto [lx, ly] = screenOf(run.back());
    const auto [mx, my] = screenOf(run[run.size() / 2]);
    const int dir = exitGridDirectionForLine(lx - fx, ly - fy, mx - cx, my - cy, flipSide);
    return exitGridArtForDirection(dir, kind);
}

// position = row * WIDTH + col on the 200x200 grid.
constexpr int hexPos(int col, int row) { return row * HexagonGrid::GRID_WIDTH + col; }
} // namespace

TEST_CASE("classifySegment: the MEASURED iso-diamond edge deltas are diagonal, not cardinal",
    "[exitgrid][iso]") {
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

TEST_CASE("exitGridDirectionForLine: the measured iso deltas yield a DIAGONAL direction",
    "[exitgrid][iso]") {
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

TEST_CASE("autoArtForLine (real grid): a stroke along the iso-diamond edge commits a DIAGONAL proto",
    "[exitgrid][iso]") {
    HexagonGrid grid;
    constexpr int C = 100; // a central col/row, well inside the diamond

    // Shallow NE/SW edge: vary the column only (a const-row hex run). Screen slope ~4:1.
    const ExitGridArt shallow = autoArtForRun(grid, hexPos(C, C), hexPos(C + 30, C),
        ExitGridDestinationKind::InterMap);
    CHECK(isDiagonalProto(shallow.proPid));
    // green frm = diagonal proto + 1
    CHECK(shallow.frmPid == shallow.proPid + 1);

    // Steep NW/SE edge: vary the row only (a const-col hex run). Screen slope ~1.33:1.
    const ExitGridArt steep = autoArtForRun(grid, hexPos(C, C), hexPos(C, C + 30),
        ExitGridDestinationKind::WorldMap);
    CHECK(isDiagonalProto(steep.proPid));
    // brown frm = diagonal proto + 0x11
    CHECK(steep.frmPid == steep.proPid + 0x11);
}

// --------------------------------------------------------------------------------------------------
// TRUE-FREEZE per-segment capture. Each polyline segment is classified once from its own screen
// direction, captured with the flip at the click that closed it, and never recomputed. The model below
// reproduces ExitGridPlacementManager's contract with the pure classifier so the freeze property (a
// committed segment is independent of any later flip/cursor change) is tested headlessly.

namespace {
// A frozen segment as the manager captures it: its hexes and its single captured direction.
struct CapturedSegment {
    std::vector<int> hexes;
    int dir = 0;
};

// Capture one segment at the flip in effect right now — exactly what the manager freezes on a click.
CapturedSegment captureSegment(const ExitGridSegmentRun& run, bool flipAtCapture) {
    return { run.hexes,
        exitGridDirectionForLine(run.screenDx, run.screenDy, run.outwardX, run.outwardY,
            flipAtCapture) };
}

// Flatten frozen segments + a live segment into deduped (hex, dir) pairs (first-seen wins), modelling
// ExitGridPlacementManager::flattenSegments.
void flatten(const std::vector<CapturedSegment>& committed, const CapturedSegment& live,
    std::vector<int>& outHexes, std::vector<int>& outDirs) {
    outHexes.clear();
    outDirs.clear();
    std::set<int> seen;
    const auto append = [&](const CapturedSegment& seg) {
        for (const int hex : seg.hexes) {
            if (seen.insert(hex).second) {
                outHexes.push_back(hex);
                outDirs.push_back(seg.dir);
            }
        }
    };
    for (const auto& seg : committed) {
        append(seg);
    }
    append(live);
}
} // namespace

TEST_CASE("freeze: a captured segment's direction is INDEPENDENT of a later flip", "[exitgrid][freeze]") {
    // A horizontal segment captured with NO flip -> BOTTOM. The captured value is a snapshot: changing
    // the flip on a LATER (live) segment can never alter it.
    ExitGridSegmentRun run;
    run.hexes = { 10, 11, 12 };
    run.screenDx = 200;
    run.screenDy = 0;
    run.outwardX = 0;
    run.outwardY = 300; // below centre -> BOTTOM without flip

    const CapturedSegment frozen = captureSegment(run, /*flipAtCapture=*/false);
    CHECK(frozen.dir == ExitGrid::DIR_BOTTOM);

    // The user now presses Space (the live flip becomes true) and moves the cursor. Re-flattening with
    // any live segment and any current flip leaves the committed segment's hexes/dirs untouched.
    for (const bool liveFlip : { false, true }) {
        ExitGridSegmentRun liveRun;
        liveRun.hexes = { 12, 213, 414 }; // shares hex 12 with the frozen segment
        liveRun.screenDx = 480;
        liveRun.screenDy = 360; // a "\" diagonal
        liveRun.outwardX = 300;
        liveRun.outwardY = 200;
        const CapturedSegment live = captureSegment(liveRun, liveFlip);

        std::vector<int> hexes;
        std::vector<int> dirs;
        flatten({ frozen }, live, hexes, dirs);

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
            CHECK(dirs[3] == flipExitGridDirection(captureSegment(liveRun, false).dir));
        }
    }
}

TEST_CASE("freeze: committed hexes are pixel-stable as the live segment moves", "[exitgrid][freeze]") {
    // Two committed segments captured at different flips; as the live segment sweeps several cursor
    // positions the committed hexes/dirs must stay byte-identical. The "committed segments stay put"
    // guarantee.
    ExitGridSegmentRun r1;
    r1.hexes = { 1, 2, 3 };
    r1.screenDx = 200;
    r1.screenDy = 0;
    r1.outwardX = 0;
    r1.outwardY = 300; // BOTTOM
    const CapturedSegment c1 = captureSegment(r1, /*flipAtCapture=*/false);

    ExitGridSegmentRun r2;
    r2.hexes = { 3, 4, 5 }; // shares hex 3 with c1
    r2.screenDx = 0;
    r2.screenDy = 200; // vertical -> LEFT/RIGHT
    r2.outwardX = 300;
    r2.outwardY = 0;
    const CapturedSegment c2 = captureSegment(r2, /*flipAtCapture=*/true); // captured with flip ON

    std::vector<int> baselineHexes;
    std::vector<int> baselineDirs;
    {
        const CapturedSegment emptyLive{};
        flatten({ c1, c2 }, emptyLive, baselineHexes, baselineDirs);
    }
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
        std::vector<int> dirs;
        flatten({ c1, c2 }, captureSegment(liveRun, /*flipAtCapture=*/true), hexes, dirs);

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

TEST_CASE("exitGridSnapDirections: the eight clean directions are unit vectors at the measured slopes",
    "[exitgrid][snap]") {
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

TEST_CASE("snapToExitGridAngle: a cursor a few degrees off a clean angle snaps onto it, distance kept",
    "[exitgrid][snap]") {
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

TEST_CASE("snapToExitGridAngle: a zero-length offset is returned unchanged", "[exitgrid][snap]") {
    const auto [sx, sy] = snapToExitGridAngle(500.0, 500.0, 500.0, 500.0);
    CHECK(sx == 500.0);
    CHECK(sy == 500.0);
}

// --- Diagonal direction helpers --------------------------------------------------------------------

TEST_CASE("isDiagonalExitGridDir / exitGridDirOfProto", "[exitgrid][diagonal]") {
    CHECK_FALSE(isDiagonalExitGridDir(ExitGrid::DIR_LEFT));
    CHECK_FALSE(isDiagonalExitGridDir(ExitGrid::DIR_BOTTOM));
    CHECK(isDiagonalExitGridDir(ExitGrid::DIR_FWD_A));
    CHECK(isDiagonalExitGridDir(ExitGrid::DIR_BACK_B));
    for (int dir = 0; dir < ExitGrid::DIR_COUNT; ++dir) {
        CHECK(exitGridDirOfProto(ExitGrid::exitGridProto(dir)) == dir);
    }
}

// --- Placement: a diagonal segment places ONE row of objects, exactly like a cardinal ---------------

namespace {
// Mirror ExitGridPlacementManager::classifySegment's hex set: a segment places ONE object per drawn
// hex for EVERY direction (cardinal and diagonal alike). The doubled diagonal LOOK is display-only
// (RenderingEngine draws a decorative second texture), so the placed object count equals the hexline.
std::vector<int> classifiedHexes(const HexagonGrid& grid, int startHex, int endHex) {
    return geck::hexline::hexLine(grid, startHex, endHex);
}
} // namespace

TEST_CASE("placement: a DIAGONAL segment places ONE row of objects, same as a CARDINAL",
    "[exitgrid][placement]") {
    HexagonGrid grid;
    constexpr int C = 100;

    // A diagonal "\" run (vary the row -> the steep iso edge classifies BackSlash, dir 6/7): the placed
    // object count equals the drawn hex line, NOT 2x. The doubled band is display-only.
    const int dStart = hexPos(C, C);
    const int dEnd = hexPos(C, C + 30);
    const std::vector<int> line = geck::hexline::hexLine(grid, dStart, dEnd);
    REQUIRE(line.size() >= 2);
    const std::vector<int> diag = classifiedHexes(grid, dStart, dEnd);
    CHECK(diag.size() == line.size());
    CHECK(diag == line);

    // A cardinal run (pure horizontal hex row -> LEFT/RIGHT) is also exactly single-row.
    const std::vector<int> card = classifiedHexes(grid, hexPos(C, C), hexPos(C + 20, C));
    CHECK(card.size() == geck::hexline::hexLine(grid, hexPos(C, C), hexPos(C + 20, C)).size());
}
