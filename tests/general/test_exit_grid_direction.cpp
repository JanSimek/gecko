#include <catch2/catch_test_macros.hpp>

#include <array>
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
using geck::exitGridOutward;
using geck::flipExitGridDirection;
using geck::HexagonGrid;
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

// The user's verified data points: a horizontal drawn line for a world/town exit -> proto 0x05000013
// (dir 3, TOP) with the brown frm ext2grd4 = 0x05000024; a "/" diagonal -> proto 0x05000014 (dir 4)
// with brown frm ext2grd5 = 0x05000025.
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
    // Screen y grows DOWNWARD. The renderer pushes the bar along this vector so the trigger hex sits
    // at the bar's inner edge. The cardinals cap their named screen edge; the diagonals match the
    // classifier's side facing.
    CHECK(exitGridOutward(ExitGrid::DIR_LEFT) == std::pair{ -1, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_RIGHT) == std::pair{ 1, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_BOTTOM) == std::pair{ 0, 1 });
    CHECK(exitGridOutward(ExitGrid::DIR_TOP) == std::pair{ 0, -1 });
    CHECK(exitGridOutward(ExitGrid::DIR_FWD_A) == std::pair{ -1, 1 });   // "/" side A faces down-left
    CHECK(exitGridOutward(ExitGrid::DIR_FWD_B) == std::pair{ 1, -1 });   // "/" side B faces up-right
    CHECK(exitGridOutward(ExitGrid::DIR_BACK_A) == std::pair{ -1, -1 }); // "\" side A faces up-left
    CHECK(exitGridOutward(ExitGrid::DIR_BACK_B) == std::pair{ 1, 1 });   // "\" side B faces down-right
}

TEST_CASE("exitGridOutward: opposite directions point opposite ways; out-of-range is {0,0}",
    "[exitgrid][outward]") {
    // The two members of each pair (left/right, bottom/top, the two "/"s, the two "\"s) are negatives.
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
        CHECK_FALSE((oa.first == 0 && oa.second == 0)); // every real direction has an outward vector
    }
    CHECK(exitGridOutward(-1) == std::pair{ 0, 0 });
    CHECK(exitGridOutward(ExitGrid::DIR_COUNT) == std::pair{ 0, 0 });
}

// --------------------------------------------------------------------------------------------------
// ISO-DIAMOND GEOMETRY LOCK
//
// The GUI "Draw edge" tool classifies a stroke by its first->last SCREEN delta (Hex::x()/y()). The
// playable area is an iso DIAMOND, so real "sloped" exits run along the diamond's two slanted edges,
// which lie along the hex grid's two axes. Their SCREEN slopes are NOT 1:1: measured on the real
// 200x200 HexagonGrid, the shallow NE/SW edge is ~4:1 and the steep NW/SE edge is ~1.33:1. A
// 45°-centred [1/2, 2] diagonal band tipped the shallow 4:1 edge to Horizontal, so a stroke drawn
// along the real diamond edge got jagged cardinal bars. The cases below feed those EXACT measured
// deltas to the classifier and assert they yield the diagonal "/" "\" art (dirs 4..7, protos
// 0x05000014..17), locking the geometry so the band can't silently regress to 45°.

namespace {
bool isDiagonalProto(uint32_t proPid) {
    return proPid >= ExitGrid::FWD_A_PRO_PID && proPid <= ExitGrid::BACK_B_PRO_PID; // 0x05000014..17
}
bool isDiagonalDir(int dir) { return dir >= ExitGrid::DIR_FWD_A && dir <= ExitGrid::DIR_BACK_B; }

// Mirror ExitGridPlacementManager::autoArtForLine's core on the REAL grid: walk the hex line, take
// the first->last screen delta for the axis and the midpoint's offset from the grid centre for the
// side, then classify. Returns the {proto, frm} the tool would commit for the whole stroke.
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

    // Shallow NE/SW diamond edge, measured first->last delta on the real grid: (-720, 180), slope 4:1.
    // With the old 45°-centred band this fell to Horizontal (the bug); it MUST be diagonal.
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
