#include <catch2/catch_test_macros.hpp>

#include <SFML/System/Vector2.hpp>

#include "editor/Hex.h"
#include "editor/HexagonGrid.h"
#include "ui/viewport/ViewportController.h"

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
