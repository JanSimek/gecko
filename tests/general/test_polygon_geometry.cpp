#include <catch2/catch_test_macros.hpp>

#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "editor/HexagonGrid.h"
#include "util/PolygonGeometry.h"

using geck::HexagonGrid;
using geck::geometry::BoundingBox;
using geck::geometry::pointInPolygon;
using geck::geometry::polygonBounds;

namespace {

// A unit square (0,0)-(10,10), counter-clockwise.
std::vector<sf::Vector2f> square() {
    return { { 0.f, 0.f }, { 10.f, 0.f }, { 10.f, 10.f }, { 0.f, 10.f } };
}

// A right triangle with vertices (0,0), (10,0), (0,10).
std::vector<sf::Vector2f> triangle() {
    return { { 0.f, 0.f }, { 10.f, 0.f }, { 0.f, 10.f } };
}

} // namespace

TEST_CASE("pointInPolygon: degenerate polygons enclose nothing", "[geometry][polygon]") {
    CHECK_FALSE(pointInPolygon({ 0.f, 0.f }, {}));
    CHECK_FALSE(pointInPolygon({ 0.f, 0.f }, { { 0.f, 0.f } }));
    CHECK_FALSE(pointInPolygon({ 0.f, 0.f }, { { 0.f, 0.f }, { 1.f, 1.f } }));
}

TEST_CASE("pointInPolygon: square interior and exterior", "[geometry][polygon]") {
    const auto poly = square();

    // Clearly inside.
    CHECK(pointInPolygon({ 5.f, 5.f }, poly));
    CHECK(pointInPolygon({ 1.f, 9.f }, poly));
    CHECK(pointInPolygon({ 9.f, 1.f }, poly));

    // Clearly outside.
    CHECK_FALSE(pointInPolygon({ -1.f, 5.f }, poly));
    CHECK_FALSE(pointInPolygon({ 5.f, -1.f }, poly));
    CHECK_FALSE(pointInPolygon({ 11.f, 5.f }, poly));
    CHECK_FALSE(pointInPolygon({ 5.f, 11.f }, poly));
    CHECK_FALSE(pointInPolygon({ 100.f, 100.f }, poly));
}

TEST_CASE("pointInPolygon: triangle interior, exterior, and a near-edge point", "[geometry][polygon]") {
    const auto poly = triangle();

    // Interior: below the hypotenuse x+y < 10.
    CHECK(pointInPolygon({ 1.f, 1.f }, poly));
    CHECK(pointInPolygon({ 4.f, 4.f }, poly));

    // Exterior: above the hypotenuse x+y > 10.
    CHECK_FALSE(pointInPolygon({ 6.f, 6.f }, poly));
    CHECK_FALSE(pointInPolygon({ 9.f, 9.f }, poly));

    // Outside the bounding box entirely.
    CHECK_FALSE(pointInPolygon({ -5.f, 5.f }, poly));
    CHECK_FALSE(pointInPolygon({ 5.f, -5.f }, poly));

    // Just inside the hypotenuse (x+y slightly < 10) reads as inside.
    CHECK(pointInPolygon({ 4.9f, 4.9f }, poly));
}

TEST_CASE("pointInPolygon: concave polygon (the classic arrow/L shape)", "[geometry][polygon]") {
    // An L-shape: the notch at the top-right must read as OUTSIDE.
    const std::vector<sf::Vector2f> lShape = {
        { 0.f, 0.f }, { 10.f, 0.f }, { 10.f, 4.f }, { 4.f, 4.f }, { 4.f, 10.f }, { 0.f, 10.f }
    };

    CHECK(pointInPolygon({ 2.f, 2.f }, lShape));       // in the wide base
    CHECK(pointInPolygon({ 2.f, 8.f }, lShape));       // in the vertical arm
    CHECK(pointInPolygon({ 8.f, 2.f }, lShape));       // in the horizontal arm
    CHECK_FALSE(pointInPolygon({ 8.f, 8.f }, lShape)); // in the notch -> outside
}

TEST_CASE("polygonBounds computes the axis-aligned bounds and contains()", "[geometry][polygon]") {
    const std::vector<sf::Vector2f> verts = { { 3.f, -2.f }, { -1.f, 5.f }, { 7.f, 1.f } };
    const BoundingBox box = polygonBounds(verts);
    CHECK(box.minX == -1.f);
    CHECK(box.maxX == 7.f);
    CHECK(box.minY == -2.f);
    CHECK(box.maxY == 5.f);

    CHECK(box.contains({ 0.f, 0.f }));
    CHECK(box.contains({ -1.f, 5.f })); // a corner is inclusive
    CHECK_FALSE(box.contains({ 8.f, 0.f }));
    CHECK_FALSE(box.contains({ 0.f, 6.f }));

    // Empty vertex list -> all-zero box that contains only the origin.
    const BoundingBox empty = polygonBounds({});
    CHECK(empty.contains({ 0.f, 0.f }));
    CHECK_FALSE(empty.contains({ 1.f, 0.f }));
}

// This mirrors what ExitGridPlacementManager::selectExitGridsInPolygon does: it ray-casts each hex
// center against the polygon to decide which hexes become exit grids. We exercise that exact
// geometry over the real HexagonGrid so the selection set is pinned without needing the Qt dialog
// or GameResources that the manager itself requires.
TEST_CASE("polygon-over-grid selects exactly the hexes whose center is inside a triangle", "[geometry][polygon][grid]") {
    HexagonGrid grid;
    REQUIRE(grid.size() == static_cast<size_t>(HexagonGrid::POSITION_COUNT));

    // Build a triangle around an arbitrary hex's world center so we know at least one hex lands
    // inside. Use a generous span so several hexes fall in the interior.
    auto anchor = grid.getHexByPosition(20000);
    REQUIRE(anchor.has_value());
    const float cx = static_cast<float>(anchor->get().x());
    const float cy = static_cast<float>(anchor->get().y());

    const std::vector<sf::Vector2f> tri = {
        { cx - 60.f, cy + 40.f },
        { cx + 60.f, cy + 40.f },
        { cx, cy - 60.f },
    };
    const BoundingBox bounds = polygonBounds(tri);

    std::vector<int> selected;
    for (int i = 0; i < static_cast<int>(grid.size()); ++i) {
        auto hex = grid.getHexByPosition(static_cast<uint32_t>(i));
        if (!hex.has_value()) {
            continue;
        }
        sf::Vector2f center(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
        if (bounds.contains(center) && pointInPolygon(center, tri)) {
            selected.push_back(i);
        }
    }

    // The anchor hex sits inside the triangle.
    CHECK(std::find(selected.begin(), selected.end(), 20000) != selected.end());
    // A non-trivial region was selected, and far-away hexes were not.
    CHECK(selected.size() > 1);
    CHECK(std::find(selected.begin(), selected.end(), 0) == selected.end());

    // Every selected hex really is inside the polygon, and (sanity) the count matches a direct
    // recomputation without the bbox cull (the bbox must never exclude a true interior hex).
    std::size_t directCount = 0;
    for (int i = 0; i < static_cast<int>(grid.size()); ++i) {
        auto hex = grid.getHexByPosition(static_cast<uint32_t>(i));
        if (!hex.has_value()) {
            continue;
        }
        sf::Vector2f center(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
        const bool inside = pointInPolygon(center, tri);
        if (inside) {
            ++directCount;
            CHECK(bounds.contains(center)); // bbox cull never drops a true interior hex
        }
    }
    CHECK(selected.size() == directCount);
}
