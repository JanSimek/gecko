#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <SFML/System/Vector2.hpp>

#include "viewport/EdgeScroll.h"

using namespace geck;
using Catch::Matchers::WithinAbs;

namespace {
constexpr float kMargin = 32.0f;
constexpr float kSpeed = 900.0f;
constexpr sf::Vector2f kViewport{ 800.0f, 600.0f };

sf::Vector2f vel(sf::Vector2f cursor) {
    return EdgeScroll::velocity(cursor, kViewport, kMargin, kSpeed);
}
} // namespace

TEST_CASE("EdgeScroll: cursor in the interior does not scroll", "[edge_scroll]") {
    const sf::Vector2f v = vel({ 400.0f, 300.0f });
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
}

TEST_CASE("EdgeScroll: just inside the margin does not scroll", "[edge_scroll]") {
    // Exactly at the margin boundary (distanceFromEdge == margin) is treated as outside.
    CHECK(vel({ kMargin, 300.0f }).x == 0.0f);
    CHECK(vel({ kViewport.x - kMargin, 300.0f }).x == 0.0f);
    CHECK(vel({ 400.0f, kMargin }).y == 0.0f);
}

TEST_CASE("EdgeScroll: near each edge scrolls toward that edge", "[edge_scroll]") {
    // Left edge -> negative X, no Y.
    const sf::Vector2f left = vel({ 8.0f, 300.0f });
    CHECK(left.x < 0.0f);
    CHECK(left.y == 0.0f);

    // Right edge -> positive X.
    const sf::Vector2f right = vel({ kViewport.x - 8.0f, 300.0f });
    CHECK(right.x > 0.0f);
    CHECK(right.y == 0.0f);

    // Top edge -> negative Y.
    const sf::Vector2f top = vel({ 400.0f, 8.0f });
    CHECK(top.y < 0.0f);
    CHECK(top.x == 0.0f);

    // Bottom edge -> positive Y.
    const sf::Vector2f bottom = vel({ 400.0f, kViewport.y - 8.0f });
    CHECK(bottom.y > 0.0f);
    CHECK(bottom.x == 0.0f);
}

TEST_CASE("EdgeScroll: speed ramps with depth into the margin", "[edge_scroll]") {
    // At the very edge (distanceFromEdge == 0) the ramp is full speed.
    CHECK_THAT(vel({ 0.0f, 300.0f }).x, WithinAbs(-kSpeed, 0.001f));

    // Halfway into the band (distanceFromEdge == margin/2) is half speed.
    CHECK_THAT(vel({ kMargin / 2.0f, 300.0f }).x, WithinAbs(-kSpeed * 0.5f, 0.001f));

    // Deeper into the band is faster than shallower.
    CHECK(std::abs(vel({ 4.0f, 300.0f }).x) > std::abs(vel({ 20.0f, 300.0f }).x));
}

TEST_CASE("EdgeScroll: a corner scrolls diagonally", "[edge_scroll]") {
    const sf::Vector2f topLeft = vel({ 4.0f, 4.0f });
    CHECK(topLeft.x < 0.0f);
    CHECK(topLeft.y < 0.0f);

    const sf::Vector2f bottomRight = vel({ kViewport.x - 4.0f, kViewport.y - 4.0f });
    CHECK(bottomRight.x > 0.0f);
    CHECK(bottomRight.y > 0.0f);
}

TEST_CASE("EdgeScroll: a cursor off the viewport does not scroll", "[edge_scroll]") {
    CHECK(vel({ -5.0f, 300.0f }).x == 0.0f);
    CHECK(vel({ kViewport.x + 5.0f, 300.0f }).x == 0.0f);
    CHECK(vel({ 400.0f, -5.0f }).y == 0.0f);
    CHECK(vel({ 400.0f, kViewport.y + 5.0f }).y == 0.0f);
}

TEST_CASE("EdgeScroll: degenerate inputs are inert", "[edge_scroll]") {
    // Zero-size viewport.
    CHECK(EdgeScroll::velocity({ 0.0f, 0.0f }, { 0.0f, 0.0f }, kMargin, kSpeed) == sf::Vector2f{});
    // Non-positive margin disables scrolling.
    CHECK(EdgeScroll::velocity({ 0.0f, 300.0f }, kViewport, 0.0f, kSpeed) == sf::Vector2f{});
}
