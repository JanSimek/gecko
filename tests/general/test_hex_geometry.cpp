#include <catch2/catch_test_macros.hpp>

#include <array>

#include "editor/HexGeometry.h"

using namespace geck::hexgrid;

namespace {

// Unit cube vectors for the six Fallout 2 hex directions (derived so the even-q
// offset<->cube conversion reproduces the engine's neighbour deltas).
constexpr std::array<Cube, 6> DIRECTION_CUBES = { {
    { -1, 1, 0 },
    { -1, 0, 1 },
    { 0, -1, 1 },
    { 1, -1, 0 },
    { 1, 0, -1 },
    { 0, 1, -1 },
} };

// Ground-truth neighbour deltas (in position space) from fallout2-ce src/tile.cc
// `_dir_tile[parity][direction]`, with the engine's hex-grid width of 200:
//   _dir_tile[0] = { -1, W-1, W, W+1, 1, -W }   (even column)
//   _dir_tile[1] = { -W-1, -1, W, 1, 1-W, -W }  (odd column)
constexpr std::array<std::array<int, 6>, 2> ENGINE_DIR_TILE = { {
    { -1, WIDTH - 1, WIDTH, WIDTH + 1, 1, -WIDTH },
    { -WIDTH - 1, -1, WIDTH, 1, 1 - WIDTH, -WIDTH },
} };

// Steps one hex from `position` along direction `d` using the cube model under test.
int stepInDirection(int position, int d) {
    const ColRow next = cubeToOffset(cubeOfPosition(position) + DIRECTION_CUBES[d]);
    return next.row * WIDTH + next.col;
}

} // namespace

TEST_CASE("offset<->cube round-trips over the grid", "[hex_geometry]") {
    for (int row = 0; row < HEIGHT; row += 7) {
        for (int col = 0; col < WIDTH; col += 5) {
            const Cube cube = offsetToCube(col, row);
            CHECK(cube.x + cube.y + cube.z == 0); // cube invariant
            CHECK(cubeToOffset(cube) == ColRow{ col, row });
        }
    }
}

TEST_CASE("cube direction steps match the fallout2-ce _dir_tile table", "[hex_geometry]") {
    // An even and an odd column away from the edges so neighbours stay on-grid.
    const int evenColPos = 40 * WIDTH + 10; // col 10 (even)
    const int oddColPos = 40 * WIDTH + 11;  // col 11 (odd)

    for (int d = 0; d < 6; ++d) {
        INFO("direction " << d);
        CHECK(stepInDirection(evenColPos, d) - evenColPos == ENGINE_DIR_TILE[0][d]);
        CHECK(stepInDirection(oddColPos, d) - oddColPos == ENGINE_DIR_TILE[1][d]);
    }
}

TEST_CASE("translate preserves a shape's cube-relative layout", "[hex_geometry]") {
    const int fromAnchor = 50 * WIDTH + 50; // even column
    const int toAnchor = 120 * WIDTH + 81;  // odd column -> different parity

    SECTION("the anchor maps onto the target anchor") {
        CHECK(translate(fromAnchor, fromAnchor, toAnchor) == toAnchor);
    }

    SECTION("a neighbour maps to the same-direction neighbour of the target") {
        for (int d = 0; d < 6; ++d) {
            const int authored = stepInDirection(fromAnchor, d);
            const auto placed = translate(authored, fromAnchor, toAnchor);
            INFO("direction " << d);
            REQUIRE(placed.has_value());
            CHECK(*placed == stepInDirection(toAnchor, d));
        }
    }

    SECTION("the cube vector relative to the anchor is preserved across parities") {
        const int authored = 47 * WIDTH + 53;
        const auto placed = translate(authored, fromAnchor, toAnchor);
        REQUIRE(placed.has_value());
        const Cube authoredRel = cubeOfPosition(authored) - cubeOfPosition(fromAnchor);
        const Cube placedRel = cubeOfPosition(*placed) - cubeOfPosition(toAnchor);
        CHECK(authoredRel == placedRel);
    }
}

TEST_CASE("translate is the identity when the anchors coincide", "[hex_geometry]") {
    const int anchor = 100 * WIDTH + 100;
    const int pos = 103 * WIDTH + 97;
    CHECK(translate(pos, anchor, anchor) == pos);
}

TEST_CASE("translate reports positions that fall off the grid", "[hex_geometry]") {
    // A shape authored near the centre stamped at a corner pushes part of it off-grid.
    const int fromAnchor = 100 * WIDTH + 100;
    const int toAnchor = 0 * WIDTH + 0;   // top-left corner
    const int authored = 98 * WIDTH + 98; // up-left of the authoring anchor
    CHECK_FALSE(translate(authored, fromAnchor, toAnchor).has_value());
}
