#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <SFML/System/Vector2.hpp>

#include <cmath>
#include <utility>

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

// Regression guard for the tile-paint alignment fix (WP-10): tile placement must
// resolve a click with the render projection (screenToTileIndex, the exact inverse
// of how tile sprites are drawn), the SAME resolver hover/selection uses — NOT by
// snapping to the nearest hex and halving (worldPosToHexIndex -> hexIndexToTileIndex),
// which picks an adjacent tile away from tile centres. This test proves the two paths
// are genuinely different (so using the correct one matters) and that the render
// projection is the one that lands on the tile under the cursor.
TEST_CASE("Tile placement uses the render projection, not hex-snapping", "[viewport][tile][placement]") {
    HexagonGrid grid;
    ViewportController viewport(&grid);

    // Rendered world-space centre of a floor tile (matches MapSpriteLoader sprite placement).
    auto tileCentre = [](int index) {
        const auto sp = indexToScreenPosition(index, false);
        return sf::Vector2f(static_cast<float>(sp.x) + TILE_WIDTH / 2.0f,
            static_cast<float>(sp.y) + TILE_HEIGHT / 2.0f);
    };
    auto centreDist = [&](int tile, sf::Vector2f pt) {
        const auto c = tileCentre(tile);
        const float dx = c.x - pt.x;
        const float dy = c.y - pt.y;
        return std::sqrt(dx * dx + dy * dy);
    };

    // OLD placement path: snap the click to the nearest hex, then halve to a tile.
    auto oldPlacementTile = [&](sf::Vector2f pt) {
        const int hex = viewport.worldPosToHexIndex(pt);
        return hex < 0 ? -1 : hexIndexToTileIndex(hex);
    };
    // NEW placement path (== the hover/selection path): nearest rendered tile centre.
    auto newPlacementTile = [&](sf::Vector2f pt) {
        const auto t = screenToTileIndex(pt.x, pt.y, false);
        return t.value_or(-1);
    };

    int divergences = 0;
    for (int index : { 3050, 4851, 5050, 5051, 1234, 6789, 8080 }) {
        const auto c = tileCentre(index);

        // At the exact centre both paths agree (the layers are only ever consistent there).
        CHECK(oldPlacementTile(c) == index);
        CHECK(newPlacementTile(c) == index);

        // Away from the centre the paths can disagree; wherever they do, the new
        // (render-projection) path must land on the tile the click visually falls in.
        for (auto [dx, dy] : { std::pair{ 30.0f, 0.0f }, std::pair{ -30.0f, 0.0f },
                 std::pair{ 0.0f, 14.0f }, std::pair{ 0.0f, -14.0f },
                 std::pair{ 24.0f, 10.0f }, std::pair{ -24.0f, -10.0f } }) {
            const sf::Vector2f pt(c.x + dx, c.y + dy);
            const int newT = newPlacementTile(pt);
            if (newT < 0) {
                continue; // off-grid margin
            }
            const int oldT = oldPlacementTile(pt);
            if (oldT != newT) {
                ++divergences;
                // The correct tile is the nearest rendered centre; the old path is never closer.
                CHECK(centreDist(newT, pt) <= centreDist(oldT, pt) + 0.001f);
            }
        }
    }

    // Zero divergences would mean the two algorithms are equivalent and the fix a no-op.
    // The whole point of WP-10 is that they are NOT: placement diverged from hover.
    CHECK(divergences > 0);
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
