#include <catch2/catch_test_macros.hpp>

#include <SFML/Graphics/Rect.hpp>

#include <algorithm>
#include <vector>

#include "util/SpatialIndex.h"

using namespace geck;

namespace {
bool contains(const std::vector<int>& v, int value) {
    return std::ranges::find(v, value) != v.end();
}
} // namespace

TEST_CASE("SpatialIndex queryArea returns only items whose bounds intersect", "[spatial_index]") {
    SpatialIndex<int> index(100.0f);
    index.addItem(1, sf::FloatRect({ 0.f, 0.f }, { 10.f, 10.f }));     // cell (0,0)
    index.addItem(2, sf::FloatRect({ 500.f, 500.f }, { 10.f, 10.f })); // cell (5,5)
    index.addItem(3, sf::FloatRect({ 50.f, 50.f }, { 10.f, 10.f }));   // cell (0,0)

    const auto nearOrigin = index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 100.f, 100.f }));
    CHECK(nearOrigin.size() == 2);
    CHECK(contains(nearOrigin, 1));
    CHECK(contains(nearOrigin, 3));
    CHECK_FALSE(contains(nearOrigin, 2));

    const auto farCorner = index.queryArea(sf::FloatRect({ 490.f, 490.f }, { 50.f, 50.f }));
    CHECK(farCorner.size() == 1);
    CHECK(contains(farCorner, 2));

    CHECK(index.queryArea(sf::FloatRect({ 2000.f, 2000.f }, { 10.f, 10.f })).empty());
}

TEST_CASE("SpatialIndex returns an item spanning multiple cells exactly once", "[spatial_index]") {
    SpatialIndex<int> index(100.0f);
    // Bounds span x,y in [50, 250] -> grid cells (0..2, 0..2) == 9 cells.
    index.addItem(42, sf::FloatRect({ 50.f, 50.f }, { 200.f, 200.f }));

    const auto results = index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 300.f, 300.f }));
    REQUIRE(results.size() == 1); // de-duplicated across the 9 cells it occupies
    CHECK(results.front() == 42);
}

TEST_CASE("SpatialIndex clear drops every item from queries", "[spatial_index]") {
    SpatialIndex<int> index(100.0f);
    index.addItem(1, sf::FloatRect({ 0.f, 0.f }, { 10.f, 10.f }));
    index.addItem(2, sf::FloatRect({ 500.f, 500.f }, { 10.f, 10.f }));

    index.clear();
    CHECK(index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 50.f, 50.f })).empty());
    CHECK(index.queryArea(sf::FloatRect({ 490.f, 490.f }, { 50.f, 50.f })).empty());
}

TEST_CASE("SpatialIndex queryArea callback can stop early", "[spatial_index]") {
    SpatialIndex<int> index(100.0f);
    for (int i = 0; i < 5; ++i) {
        index.addItem(i, sf::FloatRect({ 0.f, 0.f }, { 10.f, 10.f })); // all in cell (0,0)
    }

    int visited = 0;
    index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 50.f, 50.f }), [&visited](const int&) {
        ++visited;
        return false; // request stop after the first match
    });
    CHECK(visited == 1);
}
