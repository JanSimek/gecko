#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <SFML/System/Vector2.hpp>

#include "editor/Hex.h"
#include "editor/HexagonGrid.h"
#include "util/Constants.h"
#include "util/TileUtils.h"
#include "viewport/ViewportController.h"

using namespace geck;

namespace {

// World-space centre of a hex, as stored on the grid. positionAt() should map
// this exact point back to the same hex.
sf::Vector2f hexCentre(const HexagonGrid& grid, int position) {
    auto hex = grid.getHexByPosition(static_cast<uint32_t>(position));
    REQUIRE(hex.has_value());
    return sf::Vector2f(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
}

} // namespace

// ViewportController's world->hex/tile conversions are thin wrappers over the real
// HexagonGrid algorithm; these exercise it headlessly (no SFML window needed), which
// the gecko_core carve made possible.
TEST_CASE("ViewportController maps a hex centre back to that hex", "[viewport][hex]") {
    HexagonGrid grid;
    ViewportController viewport(&grid);

    // Mid-grid positions (their world coordinates are safely positive).
    for (int position : { 8000, 12345, 20000, 25000, 30000 }) {
        CHECK(viewport.worldPosToHexIndex(hexCentre(grid, position)) == position);
    }
}

TEST_CASE("ViewportController rejects out-of-map world positions", "[viewport][hex]") {
    HexagonGrid grid;
    ViewportController viewport(&grid);

    CHECK(viewport.worldPosToHexIndex(sf::Vector2f(-100000.f, -100000.f)) == -1);
    CHECK(viewport.worldPosToHexIndex(sf::Vector2f(100000.f, 100000.f)) == -1);
}

// "F" fits the whole map in view. The grid's extent comes from the same forward projection the
// tiles are drawn with, so the two cannot drift apart.
TEST_CASE("ViewportController fits the whole tile grid in view", "[viewport][zoom]") {
    const sf::FloatRect bounds = ViewportController::mapWorldBounds();

    // Every tile in the grid lands inside the reported bounds.
    for (int index : { 0, MAP_WIDTH - 1, (MAP_HEIGHT - 1) * MAP_WIDTH, MAP_WIDTH * MAP_HEIGHT - 1, 5050 }) {
        const auto topLeft = indexToScreenPosition(index);
        CHECK(static_cast<float>(topLeft.x) >= bounds.position.x);
        CHECK(static_cast<float>(topLeft.y) >= bounds.position.y);
        CHECK(static_cast<float>(topLeft.x) + TILE_WIDTH <= bounds.position.x + bounds.size.x);
        CHECK(static_cast<float>(topLeft.y) + TILE_HEIGHT <= bounds.position.y + bounds.size.y);
    }

    HexagonGrid grid;
    ViewportController viewport(&grid);
    viewport.initialize({ 1600u, 900u });
    viewport.fitMapInView();

    // The view now covers the grid on both axes, centred on it.
    const sf::View& view = viewport.getView();
    const sf::Vector2f half = view.getSize() / 2.0f;
    CHECK(view.getCenter().x == Catch::Approx(bounds.position.x + bounds.size.x / 2.0f));
    CHECK(view.getCenter().y == Catch::Approx(bounds.position.y + bounds.size.y / 2.0f));
    CHECK(view.getCenter().x - half.x <= bounds.position.x);
    CHECK(view.getCenter().x + half.x >= bounds.position.x + bounds.size.x);
    CHECK(view.getCenter().y - half.y <= bounds.position.y);
    CHECK(view.getCenter().y + half.y >= bounds.position.y + bounds.size.y);

    // A 1600x900 viewport needs about 0.20x, comfortably inside the zoom clamp.
    CHECK(viewport.getZoomLevel() > 0.1f);
    CHECK(viewport.getZoomLevel() < 1.0f);
}

// A viewport far too small to ever show the map still ends centred on it, at the zoom floor
// rather than at some unreachable value.
TEST_CASE("ViewportController fit stops at the minimum zoom", "[viewport][zoom]") {
    HexagonGrid grid;
    ViewportController viewport(&grid);
    viewport.initialize({ 64u, 64u });
    viewport.fitMapInView();

    const sf::FloatRect bounds = ViewportController::mapWorldBounds();
    CHECK(viewport.getZoomLevel() == Catch::Approx(0.1f)); // MIN_ZOOM
    CHECK(viewport.getView().getCenter().x == Catch::Approx(bounds.position.x + bounds.size.x / 2.0f));
}
