#include <catch2/catch_test_macros.hpp>

#include <array>

#include "editor/HexGeometry.h"

using namespace geck::hexgrid;

namespace {

// Ground-truth neighbour deltas (in position space) from fallout2-ce src/tile.cc
// `_dir_tile[parity][direction]`, with the engine's hex-grid width of 200:
//   _dir_tile[0] = { -1, W-1, W, W+1, 1, -W }   (even column)
//   _dir_tile[1] = { -W-1, -1, W, 1, 1-W, -W }  (odd column)
// These are the single-step neighbour offsets the engine walks in tileGetTileInDirection.
constexpr std::array<std::array<int, 6>, 2> ENGINE_DIR_TILE = { {
    { -1, WIDTH - 1, WIDTH, WIDTH + 1, 1, -WIDTH },
    { -WIDTH - 1, -1, WIDTH, 1, 1 - WIDTH, -WIDTH },
} };

// Steps one hex from `position` along direction `d` using the cube model under test.
int stepInDirection(int position, int d) {
    const ColRow next = cubeToOffset(cubeOfPosition(position) + directionCube(d));
    return next.row * WIDTH + next.col;
}

} // namespace

TEST_CASE("offset<->cube round-trips over the grid", "[hex_geometry]") {
    for (int row = 0; row < HEIGHT; row += 7) {
        for (int col = 0; col < WIDTH; col += 5) {
            const Cube cube = offsetToCube(col, row);
            CHECK(cube.x + cube.y + cube.z == 0); // cube invariant
            const ColRow back = cubeToOffset(cube);
            CHECK(back == ColRow{ col, row });
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

TEST_CASE("cube rotation has the expected algebraic properties", "[hex_geometry]") {
    const Cube sample{ 3, -5, 2 };

    SECTION("six steps is the identity and full multiples too") {
        CHECK(rotate(sample, 6) == sample);
        CHECK(rotate(sample, 0) == sample);
        CHECK(rotate(sample, 12) == sample);
        CHECK(rotate(sample, -6) == sample);
    }

    SECTION("three steps is a 180-degree point reflection") {
        CHECK(rotate(sample, 3) == Cube{ -sample.x, -sample.y, -sample.z });
    }

    SECTION("negative steps invert positive steps") {
        CHECK(rotate(rotate(sample, 2), -2) == sample);
        CHECK(rotate(sample, -1) == rotate(sample, 5));
    }

    SECTION("rotating one direction by k yields the (d+k) direction") {
        for (int d = 0; d < 6; ++d) {
            for (int k = 0; k < 6; ++k) {
                CHECK(rotate(directionCube(d), k) == directionCube((d + k) % 6));
            }
        }
    }
}

TEST_CASE("rotateDirection wraps facings into 0..5", "[hex_geometry]") {
    CHECK(rotateDirection(0, 1) == 1);
    CHECK(rotateDirection(5, 1) == 0);
    CHECK(rotateDirection(4, 3) == 1);
    CHECK(rotateDirection(0, -1) == 5);
    CHECK(rotateDirection(2, 6) == 2);
}

TEST_CASE("rotateAround moves a neighbour to the rotated neighbour", "[hex_geometry]") {
    const int pivot = 100 * WIDTH + 100; // well inside the grid

    // A neighbour in direction d, rotated by k steps about the pivot, must land on the
    // neighbour in direction (d+k)%6 — the engine-consistent definition of rotation.
    for (int d = 0; d < 6; ++d) {
        const int neighbour = stepInDirection(pivot, d);
        for (int k = 0; k < 6; ++k) {
            const auto rotated = rotateAround(neighbour, pivot, k);
            REQUIRE(rotated.has_value());
            const int expected = stepInDirection(pivot, (d + k) % 6);
            INFO("d=" << d << " k=" << k);
            CHECK(*rotated == expected);
        }
    }
}

TEST_CASE("rotateAround is identity for full revolutions and zero", "[hex_geometry]") {
    const int pivot = 100 * WIDTH + 100;
    const int pos = 100 * WIDTH + 103;
    CHECK(rotateAround(pos, pivot, 0) == pos);
    CHECK(rotateAround(pos, pivot, 6) == pos);
    CHECK(rotateAround(pos, pivot, -6) == pos);
}

TEST_CASE("stamp translates a pattern offset to the target anchor", "[hex_geometry]") {
    const int fromAnchor = 50 * WIDTH + 50;
    const int toAnchor = 120 * WIDTH + 80;

    // With no rotation, stamping reproduces the same offset around the new anchor.
    const int authored = stepInDirection(fromAnchor, 2); // one hex SE of the authoring anchor
    const auto placed = stamp(authored, fromAnchor, toAnchor, 0);
    REQUIRE(placed.has_value());
    CHECK(*placed == stepInDirection(toAnchor, 2));

    // The anchor itself always maps onto the target anchor, at any rotation.
    for (int k = 0; k < 6; ++k) {
        CHECK(stamp(fromAnchor, fromAnchor, toAnchor, k) == toAnchor);
    }
}

TEST_CASE("stamp reports positions that rotate off the grid", "[hex_geometry]") {
    // A hex on the top row rotated 180 degrees about a pivot near the top edge lands
    // above the grid -> nullopt rather than a wrapped, bogus position.
    const int pivot = 0 * WIDTH + 100;     // top edge
    const int neighbour = 1 * WIDTH + 100; // one row down
    CHECK_FALSE(rotateAround(neighbour, pivot, 3).has_value());
}
