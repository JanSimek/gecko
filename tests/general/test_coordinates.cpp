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

TEST_CASE("TileIndex validity reflects the 0..9999 range", "[coordinates]") {
    CHECK(TileIndex(0).isValid());
    CHECK(TileIndex(TileIndex::MAX_VALUE).isValid()); // 9999
    CHECK_FALSE(TileIndex(-1).isValid());
    CHECK_FALSE(TileIndex(TileIndex::MAX_VALUE + 1).isValid()); // 10000
    CHECK(TileIndex(42).value() == 42);
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

TEST_CASE("TileIndex arithmetic does not clamp", "[coordinates]") {
    const auto over = TileIndex(TileIndex::MAX_VALUE) + 1;
    CHECK(over.value() == TileIndex::MAX_VALUE + 1);
    CHECK_FALSE(over.isValid());

    TileIndex t(3);
    t += 10;
    CHECK(t.value() == 13);
}

TEST_CASE("Free validity helpers agree with the type boundaries", "[coordinates]") {
    CHECK(isValidHexPosition(0));
    CHECK(isValidHexPosition(HexPosition::MAX_VALUE));
    CHECK_FALSE(isValidHexPosition(HexPosition::MAX_VALUE + 1));
    CHECK(isValidTileIndex(TileIndex::MAX_VALUE));
    CHECK_FALSE(isValidTileIndex(-1));
}

TEST_CASE("Elevation conversions validate and round-trip", "[coordinates]") {
    CHECK(isValidElevation(0));
    CHECK(isValidElevation(2));
    CHECK_FALSE(isValidElevation(3));
    CHECK_FALSE(isValidElevation(-1));
    CHECK(toInt(Elevation::LEVEL_1) == 0);
    CHECK(toInt(Elevation::LEVEL_3) == 2);
    CHECK(toElevation(1) == Elevation::LEVEL_2);
}

TEST_CASE("CoordinateUtils::toValid* throw on out-of-range input", "[coordinates]") {
    using namespace CoordinateUtils;
    CHECK(toValidHexPosition(0).value() == 0);
    CHECK(toValidHexPosition(HexPosition::MAX_VALUE).value() == HexPosition::MAX_VALUE);
    CHECK_THROWS(toValidHexPosition(-1));
    CHECK_THROWS(toValidHexPosition(HexPosition::MAX_VALUE + 1));

    CHECK(toValidTileIndex(TileIndex::MAX_VALUE).value() == TileIndex::MAX_VALUE);
    CHECK_THROWS(toValidTileIndex(TileIndex::MAX_VALUE + 1));

    CHECK(toValidElevation(2) == Elevation::LEVEL_3);
    CHECK_THROWS(toValidElevation(3));
}

TEST_CASE("WorldCoords and ScreenCoords arithmetic and vector round-trips", "[coordinates]") {
    const WorldCoords w{ 1.5f, -2.0f };
    CHECK(w.x() == 1.5f);
    CHECK(w.y() == -2.0f);

    const auto sum = w + WorldCoords(0.5f, 2.0f);
    CHECK(sum.x() == 2.0f);
    CHECK(sum.y() == 0.0f);
    CHECK(WorldCoords(sf::Vector2f(3.0f, 4.0f)).toVector() == sf::Vector2f(3.0f, 4.0f));

    const ScreenCoords s{ 10, 20 };
    CHECK((s + ScreenCoords(5, -5)).x() == 15);
    CHECK((s + ScreenCoords(5, -5)).y() == 15);
    CHECK(s.toVector() == sf::Vector2i(10, 20));
}
