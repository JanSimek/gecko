#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

#include "format/map/Map.h"

#include "support/AnalyzeWrittenMap.h"
#include "support/ProStubProvider.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;

namespace {

bool hasEntry(const json& adjacency, int a, int b, const std::string& dir) {
    return std::ranges::any_of(adjacency, [&](const json& entry) {
        return entry.at("a") == a && entry.at("b") == b && entry.at("dir") == dir;
    });
}

} // namespace

// analyze's `adjacency` is ordered and directional: dir "E" means b sits immediately east (col+1)
// of a, "S" immediately south (row+1). The reverse direction is the swapped pair, same-tile
// borders are dropped, and borders against the empty tile are never counted.
TEST_CASE("analyze emits directional floor-tile adjacency", "[cli][analyze][adjacency]") {
    StubProvider provider;
    auto mapFile = Map::createEmptyMapFile();

    // Row 0: [191][194][194] — one 191->194 east border, one same-id border (dropped).
    // Row 1: [ 70][   ][   ] — one 191->70 south border; everything else touches empty cells.
    auto& tiles = mapFile.tiles[0];
    tiles[0].setFloor(191);
    tiles[1].setFloor(194);
    tiles[2].setFloor(194);
    tiles[100].setFloor(70);

    const json root = analyzeWrittenMap(std::move(mapFile), "synthetic.map", "geck_adjacency", provider);
    REQUIRE(root["maps"].size() == 1);
    const json& adjacency = root["maps"][0]["adjacency"];
    REQUIRE(adjacency.is_array());

    CHECK(adjacency.size() == 2);
    CHECK(hasEntry(adjacency, 191, 194, "E"));
    CHECK(hasEntry(adjacency, 191, 70, "S"));
    // Ordered: the reversed pairs were not observed and must not be reported.
    CHECK_FALSE(hasEntry(adjacency, 194, 191, "E"));
    CHECK_FALSE(hasEntry(adjacency, 70, 191, "S"));
    // Same-id and empty-touching borders are absent.
    CHECK_FALSE(hasEntry(adjacency, 194, 194, "E"));
    for (const auto& entry : adjacency) {
        CHECK(entry.at("count") == 1);
        CHECK(entry.contains("aName"));
        CHECK(entry.contains("bName"));
    }

    // The aggregate mirrors the per-map entries with totals.
    const json& aggregate = root["aggregate"]["adjacency"];
    CHECK(aggregate.size() == 2);
    CHECK(hasEntry(aggregate, 191, 194, "E"));
    CHECK(hasEntry(aggregate, 191, 70, "S"));
}
