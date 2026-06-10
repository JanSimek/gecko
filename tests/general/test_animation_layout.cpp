#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ui/widgets/AnimationLayout.h"

using namespace geck;

TEST_CASE("computeAnimationLayout sizes the canvas to the union of anchored frames", "[animation]") {
    // Two same-size frames, no offsets: the canvas equals the frame size and both
    // sit at the same place, so playback cannot wobble.
    std::vector<FrameBox> frames{
        { 0, 0, 10, 20 },
        { 0, 0, 10, 20 },
    };
    const AnimationLayout layout = computeAnimationLayout(frames);

    REQUIRE(layout.valid());
    CHECK(layout.canvasWidth == 10);
    CHECK(layout.canvasHeight == 20);
    REQUIRE(layout.placements.size() == 2);
    CHECK(layout.placements[0].left == layout.placements[1].left);
    CHECK(layout.placements[0].top == layout.placements[1].top);
}

TEST_CASE("computeAnimationLayout keeps a fixed anchor across differing frame sizes", "[animation]") {
    // A narrower/shorter second frame must stay bottom-centre aligned with the
    // first, not be re-centred — that re-centring is the jitter we are fixing.
    std::vector<FrameBox> frames{
        { 0, 0, 10, 20 }, // anchor: left = -5, top = -19
        { 0, 0, 6, 10 },  // anchor: left = -3, top = -9
    };
    const AnimationLayout layout = computeAnimationLayout(frames);

    REQUIRE(layout.valid());
    // Union of left=-5..5 and -3..3 -> width 10; top=-19..1 and -9..1 -> height 20.
    CHECK(layout.canvasWidth == 10);
    CHECK(layout.canvasHeight == 20);

    // Both frames share the same bottom-centre point on the canvas: centre x is
    // (left + width/2) and the bottom is (top + height).
    const FrameRect& a = layout.placements[0];
    const FrameRect& b = layout.placements[1];
    CHECK(a.left + a.width / 2 == b.left + b.width / 2);
    CHECK(a.top + a.height == b.top + b.height);
}

TEST_CASE("computeAnimationLayout accumulates per-frame offsets", "[animation]") {
    // Each frame shifts by (+2, +1) cumulatively; the canvas must grow to contain
    // the whole travelled path and placements must step by the same delta.
    std::vector<FrameBox> frames{
        { 0, 0, 4, 4 },
        { 2, 1, 4, 4 },
        { 2, 1, 4, 4 },
    };
    const AnimationLayout layout = computeAnimationLayout(frames);

    REQUIRE(layout.valid());
    REQUIRE(layout.placements.size() == 3);
    // left = cumX - w/2 with cumX = 0,2,4 -> -2,0,2; max(left+w)=6, min=-2 -> 8.
    CHECK(layout.canvasWidth == 8);
    // top = cumY - h + 1 with cumY = 0,1,2 -> -3,-2,-1; max(top+h)=3, min=-3 -> 6.
    CHECK(layout.canvasHeight == 6);
    CHECK(layout.placements[1].left - layout.placements[0].left == 2);
    CHECK(layout.placements[2].left - layout.placements[1].left == 2);
    CHECK(layout.placements[1].top - layout.placements[0].top == 1);
}

TEST_CASE("computeAnimationLayout ignores empty frames when sizing", "[animation]") {
    std::vector<FrameBox> frames{
        { 0, 0, 0, 0 }, // empty: keeps a slot, does not size the canvas
        { 0, 0, 8, 8 },
    };
    const AnimationLayout layout = computeAnimationLayout(frames);

    REQUIRE(layout.valid());
    CHECK(layout.canvasWidth == 8);
    CHECK(layout.canvasHeight == 8);
    REQUIRE(layout.placements.size() == 2); // empty frame still has a placement slot
}

TEST_CASE("computeAnimationLayout reports invalid for no renderable frames", "[animation]") {
    CHECK_FALSE(computeAnimationLayout({}).valid());
    CHECK_FALSE(computeAnimationLayout({ { 0, 0, 0, 0 } }).valid());
}
