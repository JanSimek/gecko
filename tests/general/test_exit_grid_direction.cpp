#include <catch2/catch_test_macros.hpp>

#include "util/Constants.h"
#include "util/ExitGridDirection.h"

using geck::ExitGridArt;
using geck::exitGridArtForFacing;
namespace ExitGrid = geck::ExitGrid;

namespace {
// Reference point the facing is measured from.
constexpr int CX = 1000;
constexpr int CY = 1000;
} // namespace

TEST_CASE("exitGridArtForFacing picks the TOP art for a hex above centre", "[exitgrid][direction]") {
    // Screen y grows downward, so a smaller y is above centre -> faces the TOP edge.
    const ExitGridArt art = exitGridArtForFacing(CX, CY - 100, CX, CY);
    CHECK(art.proPid == ExitGrid::RECT_TOP_PRO_PID);
    CHECK(art.frmPid == ExitGrid::RECT_TOP_FRM_PID);
}

TEST_CASE("exitGridArtForFacing picks the BOTTOM art for a hex below centre", "[exitgrid][direction]") {
    const ExitGridArt art = exitGridArtForFacing(CX, CY + 100, CX, CY);
    CHECK(art.proPid == ExitGrid::RECT_BOTTOM_PRO_PID);
    CHECK(art.frmPid == ExitGrid::RECT_BOTTOM_FRM_PID);
}

TEST_CASE("exitGridArtForFacing picks the LEFT art for a hex left of centre", "[exitgrid][direction]") {
    const ExitGridArt art = exitGridArtForFacing(CX - 100, CY, CX, CY);
    CHECK(art.proPid == ExitGrid::RECT_LEFT_PRO_PID);
    CHECK(art.frmPid == ExitGrid::RECT_LEFT_FRM_PID);
}

TEST_CASE("exitGridArtForFacing picks the RIGHT art for a hex right of centre", "[exitgrid][direction]") {
    const ExitGridArt art = exitGridArtForFacing(CX + 100, CY, CX, CY);
    CHECK(art.proPid == ExitGrid::RECT_RIGHT_PRO_PID);
    CHECK(art.frmPid == ExitGrid::RECT_RIGHT_FRM_PID);
}

TEST_CASE("exitGridArtForFacing breaks a |dx| == |dy| tie toward the vertical (top/bottom) edge", "[exitgrid][direction]") {
    // |dy| >= |dx| classifies as a vertical edge, so an exact diagonal favours top/bottom.
    const ExitGridArt up = exitGridArtForFacing(CX + 100, CY - 100, CX, CY);
    CHECK(up.proPid == ExitGrid::RECT_TOP_PRO_PID);

    const ExitGridArt down = exitGridArtForFacing(CX - 100, CY + 100, CX, CY);
    CHECK(down.proPid == ExitGrid::RECT_BOTTOM_PRO_PID);
}

TEST_CASE("exitGridArtForFacing classifies by the dominant axis when offsets differ", "[exitgrid][direction]") {
    // Mostly-horizontal offset -> a left/right edge even with some vertical component.
    const ExitGridArt right = exitGridArtForFacing(CX + 200, CY + 50, CX, CY);
    CHECK(right.proPid == ExitGrid::RECT_RIGHT_PRO_PID);

    // Mostly-vertical offset -> a top/bottom edge even with some horizontal component.
    const ExitGridArt top = exitGridArtForFacing(CX - 50, CY - 200, CX, CY);
    CHECK(top.proPid == ExitGrid::RECT_TOP_PRO_PID);
}
