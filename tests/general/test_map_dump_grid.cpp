#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <sstream>

#include "cli/MapAnalyzer.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "writer/map/MapWriter.h"

#include "support/ProStubProvider.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;

namespace {

// Write `mapFile` to a temp .map and return dump-grid's parsed JSON for `elevation`. No game data is
// mounted, so dump-grid reads the file back via cli::loadMap's disk fallback (tile ids are raw in the
// file, so they survive without tiles.lst). Mirrors the analyze tests' harness.
json dumpWrittenMap(Map::MapFile mapFile, const char* mapName, const char* tempPrefix, StubProvider& provider) {
    Map map{ mapName };
    map.setMapFile(std::make_unique<Map::MapFile>(std::move(mapFile)));

    TempFile out{ tempPrefix, ".map" };
    {
        MapWriter writer{ [&](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(out.path());
        REQUIRE(writer.write(map.getMapFile()));
    } // flush + close before dump-grid reads it back

    resource::GameResources resources;
    cli::DumpGridOptions opts;
    opts.map = out.path().string();
    opts.elevation = 0; // just elevation 0, so the assertions below are unambiguous
    opts.roof = true;
    std::ostringstream jsonOut;
    REQUIRE(cli::dumpMapGrid(resources, opts, jsonOut) == 0);
    return json::parse(jsonOut.str());
}

} // namespace

TEST_CASE("dump-grid emits the raw floor grid, roof grid and object positions", "[cli][dump-grid]") {
    StubProvider provider;
    // A WALL object: walls/critters/misc don't dereference their proto on (de)serialization, so this
    // round-trips headlessly through cli::loadMap (no data) — unlike scenery, which needs its proto.
    const uint32_t wallPid = pidOf(Pro::OBJECT_TYPE::WALL, 50);

    auto mapFile = Map::createEmptyMapFile();

    // Paint known floor cells + one roof cell on elevation 0 (index = row*100 + col).
    auto& tiles = mapFile.tiles[0];
    tiles[0].setFloor(191);  // (col0, row0)
    tiles[1].setFloor(194);  // (col1, row0)
    tiles[100].setFloor(70); // (col0, row1)
    tiles[5].setRoof(480);   // a roof cell

    // One wall object at hex (col=100, row=100) on elevation 0, facing 2.
    auto wall = std::make_shared<MapObject>();
    wall->pro_pid = wallPid;
    wall->elevation = 0;
    wall->position = 100 * 200 + 100; // 20100
    wall->direction = 2;
    mapFile.map_objects[0].push_back(wall);

    const json root = dumpWrittenMap(std::move(mapFile), "synthetic.map", "geck_dump_grid", provider);

    CHECK(root["tileCols"] == 100);
    CHECK(root["tileRows"] == 100);
    CHECK(root["hexCols"] == 200);
    CHECK(root["emptyTile"] == 1);

    REQUIRE(root["elevations"].is_array());
    REQUIRE(root["elevations"].size() == 1); // only elevation 0 was requested
    const json& e = root["elevations"][0];
    CHECK(e["elevation"] == 0);

    // Floor grid: the full 10000 cells, row-major; painted cells carry their id, the rest emptyTile.
    REQUIRE(e["floor"].is_array());
    REQUIRE(e["floor"].size() == 10000);
    CHECK(e["floor"][0] == 191);
    CHECK(e["floor"][1] == 194);
    CHECK(e["floor"][100] == 70);
    CHECK(e["floor"][2] == 1); // untouched cell -> emptyTile sentinel

    // Roof grid present (roof requested) with the one painted roof cell.
    REQUIRE(e["roof"].is_array());
    REQUIRE(e["roof"].size() == 10000);
    CHECK(e["roof"][5] == 480);

    // The scenery object, with `number` == the PID's low 24 bits (the value api:proto wants).
    REQUIRE(e["objects"].is_array());
    REQUIRE(e["objects"].size() == 1);
    const json& obj = e["objects"][0];
    CHECK(obj["number"] == 50);
    CHECK(obj["type"] == "Wall");
    CHECK(obj["hex"] == 20100);
    CHECK(obj["col"] == 100);
    CHECK(obj["row"] == 100);
    CHECK(obj["dir"] == 2);
    CHECK(obj.contains("flat"));
}

TEST_CASE("dump-grid --no-floor / --no-objects drop those sections", "[cli][dump-grid]") {
    StubProvider provider;
    auto mapFile = Map::createEmptyMapFile();
    mapFile.tiles[0][0].setFloor(191);

    Map map{ "synthetic.map" };
    map.setMapFile(std::make_unique<Map::MapFile>(std::move(mapFile)));
    TempFile out{ "geck_dump_grid_flags", ".map" };
    {
        MapWriter writer{ [&](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(out.path());
        REQUIRE(writer.write(map.getMapFile()));
    }

    resource::GameResources resources;
    cli::DumpGridOptions opts;
    opts.map = out.path().string();
    opts.elevation = 0;
    opts.floor = false;
    opts.objects = false;
    std::ostringstream jsonOut;
    REQUIRE(cli::dumpMapGrid(resources, opts, jsonOut) == 0);

    const json e = json::parse(jsonOut.str())["elevations"][0];
    CHECK_FALSE(e.contains("floor"));
    CHECK_FALSE(e.contains("objects"));
    CHECK_FALSE(e.contains("roof")); // roof defaults off
}
