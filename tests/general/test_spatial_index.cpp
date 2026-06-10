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

TEST_CASE("SpatialIndex queryArea returns only items whose bounds intersect", "[spatial]") {
    SpatialIndex<int> index(100.0f);
    index.addItem(1, sf::FloatRect({ 0.f, 0.f }, { 10.f, 10.f }));     // cell (0,0)
    index.addItem(2, sf::FloatRect({ 500.f, 500.f }, { 10.f, 10.f })); // cell (5,5)
    index.addItem(3, sf::FloatRect({ 50.f, 50.f }, { 10.f, 10.f }));   // cell (0,0)

    CHECK(index.getItemCount() == 3);

    const auto near = index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 100.f, 100.f }));
    CHECK(near.size() == 2);
    CHECK(contains(near, 1));
    CHECK(contains(near, 3));
    CHECK_FALSE(contains(near, 2));

    const auto far = index.queryArea(sf::FloatRect({ 490.f, 490.f }, { 50.f, 50.f }));
    CHECK(far.size() == 1);
    CHECK(contains(far, 2));

    CHECK(index.queryArea(sf::FloatRect({ 2000.f, 2000.f }, { 10.f, 10.f })).empty());
}

TEST_CASE("SpatialIndex returns an item spanning multiple cells exactly once", "[spatial]") {
    SpatialIndex<int> index(100.0f);
    // Bounds span x,y in [50, 250] -> grid cells (0..2, 0..2) == 9 cells.
    index.addItem(42, sf::FloatRect({ 50.f, 50.f }, { 200.f, 200.f }));
    CHECK(index.getCellCount() == 9);

    const auto results = index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 300.f, 300.f }));
    REQUIRE(results.size() == 1); // de-duplicated across the 9 cells it occupies
    CHECK(results.front() == 42);
}

TEST_CASE("SpatialIndex updateItem moves the item between cells", "[spatial]") {
    SpatialIndex<int> index(100.0f);
    const auto id = index.addItem(7, sf::FloatRect({ 0.f, 0.f }, { 10.f, 10.f }));

    CHECK(contains(index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 50.f, 50.f })), 7));
    CHECK_FALSE(contains(index.queryArea(sf::FloatRect({ 500.f, 500.f }, { 50.f, 50.f })), 7));

    index.updateItem(id, sf::FloatRect({ 500.f, 500.f }, { 10.f, 10.f }));

    CHECK_FALSE(contains(index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 50.f, 50.f })), 7));
    CHECK(contains(index.queryArea(sf::FloatRect({ 500.f, 500.f }, { 50.f, 50.f })), 7));
    CHECK(index.getItemCount() == 1);
}

TEST_CASE("SpatialIndex removeItem and clear drop items from queries", "[spatial]") {
    SpatialIndex<int> index(100.0f);
    const auto a = index.addItem(1, sf::FloatRect({ 0.f, 0.f }, { 10.f, 10.f }));
    index.addItem(2, sf::FloatRect({ 0.f, 0.f }, { 10.f, 10.f }));

    index.removeItem(a);
    CHECK(index.getItemCount() == 1);
    const auto after = index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 50.f, 50.f }));
    CHECK_FALSE(contains(after, 1));
    CHECK(contains(after, 2));

    index.clear();
    CHECK(index.getItemCount() == 0);
    CHECK(index.getCellCount() == 0);
    CHECK(index.queryArea(sf::FloatRect({ 0.f, 0.f }, { 50.f, 50.f })).empty());
}

TEST_CASE("SpatialIndex queryArea callback can stop early", "[spatial]") {
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
