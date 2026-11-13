#include <catch2/catch_test_macros.hpp>

#include "rendering/MapSpriteLoader.h"

#include "support/ControllerFixture.h"

using geck::test::ControllerFixture;

TEST_CASE("Loading the objects of an absent elevation leaves the view empty", "[map_sprite_loader]") {
    // Removing the shown elevation refreshes it before the editor switches to another one.
    ControllerFixture fx;
    REQUIRE(fx.controller.setElevationPresent(1, false));
    REQUIRE_FALSE(fx.mapFile().map_objects.contains(1));

    REQUIRE_NOTHROW(fx.spriteLoader.loadObjectSprites(*fx.map, 1, fx.objects, fx.overlays));
    CHECK(fx.objects.empty());
}
