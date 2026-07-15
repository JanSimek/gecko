#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include "cli/MapGraph.h"
#include "editor/Reachability.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/FileIo.h"
#include "writer/map/MapWriter.h"

#include "support/ProStubProvider.h"

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using nlohmann::json;
using namespace geck;
using namespace geck::test;
namespace fs = std::filesystem;

namespace {

constexpr int kOpenHexA = 10100; // interior hexes, comfortably on-grid
constexpr int kOpenHexB = 10110;
constexpr int kSealedHex = 20100;

std::shared_ptr<MapObject> makeExitGrid(int position, int destMap, int destHex, int destElevation) {
    auto exit = std::make_shared<MapObject>();
    exit->position = position;
    exit->pro_pid = pidOf(Pro::OBJECT_TYPE::MISC, MapObject::EXIT_GRID_PID_INDEX_FIRST);
    exit->exit_map = destMap;
    exit->exit_position = destHex;
    exit->exit_elevation = destElevation;
    return exit;
}

std::shared_ptr<MapObject> makeWall(int position) {
    auto wall = std::make_shared<MapObject>();
    wall->position = position;
    wall->pro_pid = pidOf(Pro::OBJECT_TYPE::WALL, 1);
    return wall;
}

void writeMap(const fs::path& path, Map::MapFile mapFile) {
    Map map{ path.filename().string() };
    map.setMapFile(std::make_unique<Map::MapFile>(std::move(mapFile)));
    const StubProvider provider; // walls/misc never dereference their Pro
    MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
    writer.openFile(path);
    REQUIRE(writer.write(map.getMapFile()));
}

const json* findEdge(const json& edges, const std::string& from, const std::string& to) {
    for (const auto& edge : edges) {
        if (edge.at("from") == from && edge.at("to") == to) {
            return &edge;
        }
    }
    return nullptr;
}

// maps.txt (destination indices -> names) plus the fixture maps under <base>/maps/. mapa exits to
// every other map and its own grid is open, so return trips INTO mapa always work; each
// destination exercises one one-way verdict.
void writeFixture(const fs::path& base) {
    fs::create_directories(base / "maps");
    geck::io::writeFile(base / "data" / "maps.txt",
        "[Map 0]\nlookup_name=A\nmap_name=mapa\n"
        "[Map 1]\nlookup_name=B\nmap_name=mapb\n"
        "[Map 2]\nlookup_name=C\nmap_name=mapc\n"
        "[Map 3]\nlookup_name=D\nmap_name=mapd\n"
        "[Map 4]\nlookup_name=E\nmap_name=mape\n"
        "[Map 5]\nlookup_name=F\nmap_name=mapf\n"
        "[Map 6]\nlookup_name=G\nmap_name=mapg\n");

    auto a = Map::createEmptyMapFile();
    a.map_objects[0].push_back(makeExitGrid(100, 1, kOpenHexB, 0));
    a.map_objects[0].push_back(makeExitGrid(102, 2, kOpenHexB, 0));
    a.map_objects[0].push_back(makeExitGrid(104, 3, kOpenHexB, 0));
    a.map_objects[0].push_back(makeExitGrid(106, 4, kOpenHexA, 0));
    a.map_objects[0].push_back(makeExitGrid(108, 5, kOpenHexA, 0));
    a.map_objects[0].push_back(makeExitGrid(110, 6, kOpenHexA, 0));
    writeMap(base / "maps" / "mapa.map", std::move(a));

    // mapb: an open return exit -> two-way.
    auto b = Map::createEmptyMapFile();
    b.map_objects[0].push_back(makeExitGrid(kOpenHexB + 6, 0, 100, 0));
    writeMap(base / "maps" / "mapb.map", std::move(b));

    // mapc: no exit grids at all -> a->c has no return edge.
    writeMap(base / "maps" / "mapc.map", Map::createEmptyMapFile());

    // (mapd is never written or analysed -> a->d stays undetermined.)

    // mape: the return exit sits in a hex sealed by a full wall ring, cut off from the arrival hex.
    auto e = Map::createEmptyMapFile();
    e.map_objects[0].push_back(makeExitGrid(kSealedHex, 0, 106, 0));
    for (const int neighbour : reachability::hexNeighbors(kSealedHex)) {
        e.map_objects[0].push_back(makeWall(neighbour));
    }
    writeMap(base / "maps" / "mape.map", std::move(e));

    // mapf: the only return exit lives on another elevation (stairs) -> undeterminable.
    auto f = Map::createEmptyMapFile();
    f.map_objects[1].push_back(makeExitGrid(kOpenHexA, 0, 108, 0));
    writeMap(base / "maps" / "mapf.map", std::move(f));

    // mapg: a sealed same-elevation return PLUS a return on another elevation — the stairs
    // option must veto the "return-unreachable" verdict.
    auto g = Map::createEmptyMapFile();
    g.map_objects[0].push_back(makeExitGrid(kSealedHex, 0, 110, 0));
    for (const int neighbour : reachability::hexNeighbors(kSealedHex)) {
        g.map_objects[0].push_back(makeWall(neighbour));
    }
    g.map_objects[1].push_back(makeExitGrid(kOpenHexB, 0, 110, 0));
    writeMap(base / "maps" / "mapg.map", std::move(g));
}

} // namespace

// The one-way join: each map-kind edge is crossed with the reachability model — a destination
// with no exit grid back is "no-return-edge"; return exits sealed away from the arrival hexes are
// "return-unreachable"; a walkable return (or an unanalysed / stairs-only destination) is not
// flagged. Built on a written-map fixture: maps.txt gives the destination indices names, and the
// .map files are read back through cli::loadMap's disk fallback.
TEST_CASE("map_graph flags one-way edges via the reachability join", "[cli][mapgraph]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "mapgraph";
    fs::remove_all(base);
    writeFixture(base);

    resource::GameResources resources;
    resources.files().addDataPath(base.string());

    cli::MapGraphOptions options;
    for (const char* name : { "mapa.map", "mapb.map", "mapc.map", "mape.map", "mapf.map", "mapg.map" }) {
        options.maps.push_back((base / "maps" / name).string());
    }
    std::ostringstream out;
    REQUIRE(cli::buildMapGraph(resources, options, out) == 0);
    const json root = json::parse(out.str());
    const json& edges = root.at("edges");

    SECTION("a walkable return path is two-way in both directions") {
        const json* aToB = findEdge(edges, "mapa.map", "mapb.map");
        REQUIRE(aToB != nullptr);
        CHECK(aToB->at("oneWay") == false);
        CHECK(aToB->at("oneWayReason").is_null());
        const json* bToA = findEdge(edges, "mapb.map", "mapa.map");
        REQUIRE(bToA != nullptr);
        CHECK(bToA->at("oneWay") == false);
    }

    SECTION("a destination with no exit grid back is one-way: no-return-edge") {
        const json* aToC = findEdge(edges, "mapa.map", "mapc.map");
        REQUIRE(aToC != nullptr);
        CHECK(aToC->at("oneWay") == true);
        CHECK(aToC->at("oneWayReason") == "no-return-edge");
    }

    SECTION("an unanalysed destination stays undetermined") {
        const json* aToD = findEdge(edges, "mapa.map", "mapd.map");
        REQUIRE(aToD != nullptr);
        CHECK(aToD->at("kind") == "map");
        CHECK(aToD->at("oneWay").is_null());
        CHECK(aToD->at("oneWayReason").is_null());
    }

    SECTION("a return exit sealed away from the arrival hex is one-way: return-unreachable") {
        const json* aToE = findEdge(edges, "mapa.map", "mape.map");
        REQUIRE(aToE != nullptr);
        CHECK(aToE->at("oneWay") == true);
        CHECK(aToE->at("oneWayReason") == "return-unreachable");
        // The reverse trip lands in mapa's open grid right next to its own return exit.
        const json* eToA = findEdge(edges, "mape.map", "mapa.map");
        REQUIRE(eToA != nullptr);
        CHECK(eToA->at("oneWay") == false);
    }

    SECTION("a return that only runs through another elevation stays undetermined") {
        const json* aToF = findEdge(edges, "mapa.map", "mapf.map");
        REQUIRE(aToF != nullptr);
        CHECK(aToF->at("oneWay").is_null());
        // The reverse direction is decidable: the return exit into mapf's arrival elevation exists.
        const json* fToA = findEdge(edges, "mapf.map", "mapa.map");
        REQUIRE(fToA != nullptr);
        CHECK(fToA->at("oneWay") == false);
    }

    SECTION("a sealed same-elevation return plus a stairs return stays undetermined") {
        const json* aToG = findEdge(edges, "mapa.map", "mapg.map");
        REQUIRE(aToG != nullptr);
        CHECK(aToG->at("oneWay").is_null());
        CHECK(aToG->at("oneWayReason").is_null());
    }

    SECTION("stats lists exactly the flagged one-way edges") {
        const json& oneWay = root.at("stats").at("oneWayEdges");
        REQUIRE(oneWay.size() == 2);
        CHECK(findEdge(oneWay, "mapa.map", "mapc.map") != nullptr);
        CHECK(findEdge(oneWay, "mapa.map", "mape.map") != nullptr);
    }

    fs::remove_all(base);
}
