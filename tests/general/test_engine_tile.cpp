#include <catch2/catch_test_macros.hpp>

#include "cli/EngineTile.h"

using namespace geck;

// These are the properties the engine's own code depends on, so a faithful port must have them:
// make_straight_path starts from the pixel at a tile's centre and treats the tile found there as the
// start tile; tile_dist reaches an adjacent tile in one tile_dir step (melee range is distance 1);
// obj_can_see_obj sees an adjacent critter because the walk enters the target's tile before its last pixel.
TEST_CASE("engine tile math holds the invariants the engine relies on", "[engine_tile]") {
    const int centre = 100 * enginetile::GRID_WIDTH + 100;
    const auto camera = enginetile::cameraCenteredOn(centre);

    SECTION("a tile's centre pixel maps back to that tile") {
        for (int tile : { centre, centre + 1, centre - 1, centre + 3 * enginetile::GRID_WIDTH + 7, centre - 9 * enginetile::GRID_WIDTH - 4 }) {
            const auto xy = enginetile::tileToScreenXY(tile, camera);
            REQUIRE(xy.has_value());
            CHECK(enginetile::tileFromScreenXY((*xy)[0] + 16, (*xy)[1] + 8, camera) == tile);
        }
    }

    SECTION("each direction step is a neighbour one tile away, in that direction") {
        for (int start : { centre, centre + 1, centre + 7 * enginetile::GRID_WIDTH + 3 }) {
            for (int rotation = 0; rotation < enginetile::ROTATION_COUNT; ++rotation) {
                const int neighbour = enginetile::tileInDirection(start, rotation, 1);
                CHECK(enginetile::rotationTo(start, neighbour, camera) == rotation);
                CHECK(enginetile::distance(start, neighbour, camera) == 1);
            }
        }
        CHECK(enginetile::distance(centre, centre, camera) == 0);
    }

    SECTION("the straight path finds a blocker standing on the target") {
        for (int rotation = 0; rotation < enginetile::ROTATION_COUNT; ++rotation) {
            const int target = enginetile::tileInDirection(centre, rotation, 3);
            const auto path = enginetile::straightPath(centre, target, camera, [target](int tile) { return tile == target ? 7 : -1; });
            CHECK(path.obstacleId == 7);
            CHECK(path.obstacleTile == target);
        }
    }

    SECTION("a blocker on the start tile stops the walk at once") {
        const auto path = enginetile::straightPath(centre, centre + 5, camera, [](int tile) { return tile == centre ? 1 : -1; });
        CHECK(path.obstacleTile == centre);
        CHECK(path.tilesEntered.size() == 1);
    }

    SECTION("the frontal arc is the facing and one step either side") {
        const int target = enginetile::tileInDirection(centre, 2, 4);
        const int towards = enginetile::rotationTo(centre, target, camera);
        CHECK(enginetile::inFrontalArc(centre, towards, target, camera));
        CHECK(enginetile::inFrontalArc(centre, (towards + 1) % 6, target, camera));
        CHECK(enginetile::inFrontalArc(centre, (towards + 5) % 6, target, camera));
        CHECK_FALSE(enginetile::inFrontalArc(centre, (towards + 3) % 6, target, camera));
    }

    SECTION("stepping stops at the grid edge") {
        CHECK(enginetile::isEdge(0));
        CHECK(enginetile::tileInDirection(0, 3, 5) == 0);
        CHECK(enginetile::distance(-1, centre, camera) == enginetile::MAX_DISTANCE);
    }
}
