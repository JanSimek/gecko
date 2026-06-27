#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>

#include "util/Constants.h"
#include "util/ExitGridDirection.h"

using geck::ExitGridArt;
using geck::exitGridArtForDirection;
using geck::exitGridArtForFacing;
using geck::exitGridArtForSegment;
using geck::ExitGridDestinationKind;
using geck::exitGridDirectionForLine;
using geck::exitGridOutward;
using geck::flipExitGridDirection;
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
