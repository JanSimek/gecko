#include <catch2/catch_test_macros.hpp>

#include <SFML/System/Vector2.hpp>

#include "util/Coordinates.h"

using namespace geck;

TEST_CASE("HexPosition validity reflects the 0..39999 range", "[coordinates]") {
    CHECK(HexPosition(0).isValid());
    CHECK(HexPosition(HexPosition::MAX_VALUE).isValid()); // 39999
    CHECK_FALSE(HexPosition(-1).isValid());
    CHECK_FALSE(HexPosition(HexPosition::MAX_VALUE + 1).isValid()); // 40000
    CHECK(HexPosition(123).value() == 123);
}

TEST_CASE("HexPosition arithmetic does not clamp; isValid catches overflow", "[coordinates]") {
    // The wrapper never clamps, so callers must validate with isValid(). The
    // CLAUDE.md hex-vs-tile range rule depends on this staying true.
    const auto over = HexPosition(HexPosition::MAX_VALUE) + 1;
    CHECK(over.value() == HexPosition::MAX_VALUE + 1);
    CHECK_FALSE(over.isValid());

    const auto under = HexPosition(0) - 1;
    CHECK(under.value() == -1);
    CHECK_FALSE(under.isValid());

    HexPosition p(10);
    p += 5;
    CHECK(p.value() == 15);
    p -= 20;
    CHECK(p.value() == -5);
    CHECK_FALSE(p.isValid());
}

TEST_CASE("Free validity helpers agree with the grid boundaries", "[coordinates]") {
    CHECK(isValidHexPosition(0));
    CHECK(isValidHexPosition(HexPosition::MAX_VALUE));
    CHECK_FALSE(isValidHexPosition(HexPosition::MAX_VALUE + 1));
    CHECK(isValidTileIndex(TILES_PER_ELEVATION - 1));
    CHECK_FALSE(isValidTileIndex(TILES_PER_ELEVATION));
    CHECK_FALSE(isValidTileIndex(-1));
}

TEST_CASE("WorldCoords arithmetic and vector round-trips", "[coordinates]") {
    const WorldCoords w{ 1.5f, -2.0f };
    CHECK(w.x() == 1.5f);
    CHECK(w.y() == -2.0f);

    const auto sum = w + WorldCoords(0.5f, 2.0f);
    CHECK(sum.x() == 2.0f);
    CHECK(sum.y() == 0.0f);
    CHECK(WorldCoords(sf::Vector2f(3.0f, 4.0f)).toVector() == sf::Vector2f(3.0f, 4.0f));
}
