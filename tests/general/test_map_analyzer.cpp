#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "format/pro/Pro.h"

#include "support/AnalyzeWrittenMap.h"
#include "support/MapObjectBuilder.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;

namespace {

// Find the first scripts[] entry whose section matches `section`.
const json* findScript(const json& scripts, const std::string& section) {
    for (const auto& entry : scripts) {
        if (entry.at("section") == section) {
            return &entry;
        }
    }
    return nullptr;
}

// Whether `adjacency` holds the directional border a -> b.
bool hasEntry(const json& adjacency, int a, int b, const std::string& dir) {
    return std::ranges::any_of(adjacency, [&](const json& entry) {
        return entry.at("a") == a && entry.at("b") == b && entry.at("dir") == dir;
    });
}

} // namespace

// analyze emits a per-map `scripts` list (mirroring the editor's Scripts panel) covering every
// section, each script's resolved program index, owner object, section-specific radius/timer, and
// its slice of the map's local-variable pool — and a critter's attached `script` carries the same
// localVars. Built on a synthetic .map written headlessly (no game data mounted): scrname.msg names
// don't resolve, so this asserts the structural fields, not the friendly names.
TEST_CASE("analyze surfaces per-section scripts with their local variables", "[map_analyzer][cli]") {
    const uint32_t critterPid = ProtoId(ObjectType::Critter, 50).pid();

    auto mapFile = Map::createEmptyMapFile();

    // A flat LVAR pool shared by every script; each script owns a [offset, offset+count) slice.
    mapFile.map_local_vars = { 11, 22, 33, 44, 55, 66, 77 };
    mapFile.header.num_local_vars = static_cast<uint32_t>(mapFile.map_local_vars.size());

    // System script (section 0): program index 7, locals {11,22,33}. No owner object.
    MapScript system{};
    system.pid = MapScript::makeSid(MapScript::ScriptType::SYSTEM, 0);
    system.script_id = 7;
    system.script_oid = MapScript::NONE; // -> ownerObject null
    system.local_var_offset = 0;
    system.local_var_count = 3;
    mapFile.map_scripts[static_cast<int>(MapScript::ScriptType::SYSTEM)].push_back(system);

    // Spatial script (section 1): program index 9, radius 4, locals {44,55}.
    MapScript spatial{};
    spatial.pid = MapScript::makeSid(MapScript::ScriptType::SPATIAL, 0);
    spatial.script_id = 9;
    spatial.script_oid = 0; // 0 == none -> ownerObject null
    spatial.spatial_radius = 4;
    spatial.local_var_offset = 3;
    spatial.local_var_count = 2;
    mapFile.map_scripts[static_cast<int>(MapScript::ScriptType::SPATIAL)].push_back(spatial);

    // Critter script (section 4): program index 12, owner object 1234, locals {66,77}. A critter
    // object points at it through its map_scripts_pid (== this script's SID/pid).
    const uint32_t critterSid = MapScript::makeSid(MapScript::ScriptType::CRITTER, 0);
    MapScript critterScript{};
    critterScript.pid = critterSid;
    critterScript.script_id = 12;
    critterScript.script_oid = 1234;
    critterScript.local_var_offset = 5;
    critterScript.local_var_count = 2;
    mapFile.map_scripts[static_cast<int>(MapScript::ScriptType::CRITTER)].push_back(critterScript);

    // The critter instance carrying that script.
    addObject(mapFile, critterPid, 100 * HexagonGrid::GRID_WIDTH + 100, 0, static_cast<int32_t>(critterSid));

    const json root = analyzeWrittenMap(mapFile, "geck_analyze_scripts");
    REQUIRE(root["maps"].is_array());
    REQUIRE(root["maps"].size() == 1);
    const json& entry = root["maps"][0];

    REQUIRE(entry.contains("scripts"));
    const json& scripts = entry["scripts"];
    REQUIRE(scripts.is_array());
    REQUIRE(scripts.size() == 3);

    SECTION("the System script reports its program index and local vars, no owner") {
        const json* s = findScript(scripts, "System");
        REQUIRE(s != nullptr);
        CHECK((*s)["programIndex"] == 7);
        CHECK((*s)["ownerObject"].is_null());
        CHECK_FALSE(s->contains("spatialRadius")); // only on Spatial
        CHECK_FALSE(s->contains("timerMs"));       // only on Timer
        CHECK((*s)["localVars"] == json::array({ 11, 22, 33 }));
    }

    SECTION("the Spatial script carries spatialRadius and its own slice") {
        const json* s = findScript(scripts, "Spatial");
        REQUIRE(s != nullptr);
        CHECK((*s)["programIndex"] == 9);
        CHECK((*s)["ownerObject"].is_null()); // script_oid 0 == none
        CHECK(s->contains("spatialRadius"));
        CHECK((*s)["spatialRadius"] == 4);
        CHECK_FALSE(s->contains("timerMs"));
        CHECK((*s)["localVars"] == json::array({ 44, 55 }));
    }

    SECTION("the Critter script reports its owner object and slice") {
        const json* s = findScript(scripts, "Critter");
        REQUIRE(s != nullptr);
        CHECK((*s)["programIndex"] == 12);
        CHECK((*s)["ownerObject"] == 1234);
        CHECK((*s)["localVars"] == json::array({ 66, 77 }));
    }

    SECTION("the critter's attached script includes the same local vars") {
        REQUIRE(entry["critters"].is_array());
        REQUIRE(entry["critters"].size() == 1);
        const json& attached = entry["critters"][0]["script"];
        REQUIRE(attached.is_object());
        CHECK(attached["programIndex"] == 12);
        CHECK(attached["localVars"] == json::array({ 66, 77 }));
    }
}

// A stale local_var_count (more than the pool holds) must never read past the LVAR vector: the
// emitted slice stops at the pool's end rather than over-reading.
TEST_CASE("analyze clamps a script's local-var slice to the pool", "[map_analyzer][cli]") {
    auto mapFile = Map::createEmptyMapFile();
    mapFile.map_local_vars = { 1, 2 };
    mapFile.header.num_local_vars = static_cast<uint32_t>(mapFile.map_local_vars.size());

    MapScript system{};
    system.pid = MapScript::makeSid(MapScript::ScriptType::SYSTEM, 0);
    system.script_id = 3;
    system.script_oid = MapScript::NONE;
    system.local_var_offset = 1;
    system.local_var_count = 5; // stale: only one value (index 1) actually exists
    mapFile.map_scripts[static_cast<int>(MapScript::ScriptType::SYSTEM)].push_back(system);

    const json root = analyzeWrittenMap(mapFile, "geck_analyze_scripts_clamp");
    const json& scripts = root["maps"][0]["scripts"];
    REQUIRE(scripts.size() == 1);
    CHECK(scripts[0]["localVars"] == json::array({ 2 })); // only the in-range value, not five
}

// analyze's `adjacency` is ordered and directional: dir "E" means b sits immediately east (col+1)
// of a, "S" immediately south (row+1). The reverse direction is the swapped pair, same-tile
// borders are dropped, and borders against the empty tile are never counted.
TEST_CASE("analyze emits directional floor-tile adjacency", "[map_analyzer][cli]") {
    auto mapFile = Map::createEmptyMapFile();

    // Row 0: [191][194][194] — one 191->194 east border, one same-id border (dropped).
    // Row 1: [ 70][   ][   ] — one 191->70 south border; everything else touches empty cells.
    auto& tiles = mapFile.tiles[0];
    tiles[0].setFloor(191);
    tiles[1].setFloor(194);
    tiles[2].setFloor(194);
    tiles[100].setFloor(70);

    const json root = analyzeWrittenMap(mapFile, "geck_adjacency");
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
