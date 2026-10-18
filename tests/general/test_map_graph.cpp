#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <ostream>
#include <string>

#include "cli/MapGraph.h"
#include "editor/HexGeometry.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"

#include "support/CliJson.h"
#include "support/DataTree.h"
#include "support/MapFileIo.h"
#include "support/MapObjectBuilder.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;
namespace fs = std::filesystem;

namespace {

constexpr int kOpenHexA = 10100; // interior hexes, comfortably on-grid
constexpr int kOpenHexB = 10110;
constexpr int kSealedHex = 20100;

// Places an exit grid on `elevation` of `mapFile` leading to `destHex` on `destElevation` of `destMap`.
void addExitGrid(Map::MapFile& mapFile, int position, int destMap, int destHex, int destElevation, int elevation = 0) {
    auto exit = addObject(mapFile, ProtoId(MiscProtoTypeId::FirstExitGrid).pid(), position, elevation);
    exit->exit_map = static_cast<uint32_t>(destMap);
    exit->exit_position = static_cast<uint32_t>(destHex);
    exit->exit_elevation = static_cast<uint32_t>(destElevation);
}

// Rings `hex` with walls, sealing it off from the rest of its elevation.
void sealHex(Map::MapFile& mapFile, int hex) {
    for (const int neighbour : hexgrid::neighbors(hex)) {
        addObject(mapFile, ProtoId(ObjectType::Wall, 1).pid(), neighbour);
    }
}

json buildGraph(resource::GameResources& resources, const cli::MapGraphOptions& options) {
    return runJson([&](std::ostream& out) { return cli::buildMapGraph(resources, options, out); });
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
    test::writeFile(base / "data" / "maps.txt",
        "[Map 0]\nlookup_name=A\nmap_name=mapa\n"
        "[Map 1]\nlookup_name=B\nmap_name=mapb\n"
        "[Map 2]\nlookup_name=C\nmap_name=mapc\n"
        "[Map 3]\nlookup_name=D\nmap_name=mapd\n"
        "[Map 4]\nlookup_name=E\nmap_name=mape\n"
        "[Map 5]\nlookup_name=F\nmap_name=mapf\n"
        "[Map 6]\nlookup_name=G\nmap_name=mapg\n");

    auto a = Map::createEmptyMapFile();
    addExitGrid(a, 100, 1, kOpenHexB, 0);
    addExitGrid(a, 102, 2, kOpenHexB, 0);
    addExitGrid(a, 104, 3, kOpenHexB, 0);
    addExitGrid(a, 106, 4, kOpenHexA, 0);
    addExitGrid(a, 108, 5, kOpenHexA, 0);
    addExitGrid(a, 110, 6, kOpenHexA, 0);
    writeMapFile(a, base / "maps" / "mapa.map");

    // mapb: an open return exit -> two-way.
    auto b = Map::createEmptyMapFile();
    addExitGrid(b, kOpenHexB + 6, 0, 100, 0);
    writeMapFile(b, base / "maps" / "mapb.map");

    // mapc: no exit grids at all -> a->c has no return edge.
    writeMapFile(Map::createEmptyMapFile(), base / "maps" / "mapc.map");

    // (mapd is never written or analysed -> a->d stays undetermined.)

    // mape: the return exit sits in a hex sealed by a full wall ring, cut off from the arrival hex.
    auto e = Map::createEmptyMapFile();
    addExitGrid(e, kSealedHex, 0, 106, 0);
    sealHex(e, kSealedHex);
    writeMapFile(e, base / "maps" / "mape.map");

    // mapf: the only return exit lives on another elevation (stairs) -> undeterminable.
    auto f = Map::createEmptyMapFile();
    addExitGrid(f, kOpenHexA, 0, 108, 0, 1);
    writeMapFile(f, base / "maps" / "mapf.map");

    // mapg: a sealed same-elevation return PLUS a return on another elevation — the stairs
    // option must veto the "return-unreachable" verdict.
    auto g = Map::createEmptyMapFile();
    addExitGrid(g, kSealedHex, 0, 110, 0);
    sealHex(g, kSealedHex);
    addExitGrid(g, kOpenHexB, 0, 110, 0, 1);
    writeMapFile(g, base / "maps" / "mapg.map");
}

} // namespace

// The one-way join: each map-kind edge is crossed with the reachability model — a destination
// with no exit grid back is "no-return-edge"; return exits sealed away from the arrival hexes are
// "return-unreachable"; a walkable return (or an unanalysed / stairs-only destination) is not
// flagged. Built on a written-map fixture: maps.txt gives the destination indices names, and the
// .map files are read back through cli::loadMap's disk fallback.
TEST_CASE("map_graph flags one-way edges via the reachability join", "[cli][mapgraph]") {
    test::DataTree tree{ "mapgraph" };
    const fs::path& base = tree.root();
    writeFixture(base);

    auto& resources = tree.mount();

    cli::MapGraphOptions options;
    for (const char* name : { "mapa.map", "mapb.map", "mapc.map", "mape.map", "mapf.map", "mapg.map" }) {
        options.maps.push_back((base / "maps" / name).string());
    }
    const json root = buildGraph(resources, options);
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
}

// Script transitions: a load_map call becomes an edge from every map that places its script — the
// map's own (header) script, 1-based, or a map_scripts entry, 0-based — whichever way the script
// names the target. A script no map places keeps its call in unplacedScriptCalls.
TEST_CASE("map_graph adds load_map transitions from the maps that place each script", "[cli][mapgraph]") {
    test::DataTree tree{ "mapgraph_scripts" };
    const fs::path& base = tree.root();
    fs::create_directories(base / "maps");
    tree.write("data/maps.txt",
        "[Map 0]\nlookup_name=A\nmap_name=mapa\n[Map 1]\nlookup_name=B\nmap_name=mapb\n[Map 2]\nlookup_name=C\nmap_name=mapc\n");
    tree.write("scripts/scripts.lst", "town.int ; a's map script\nguard.int ; b's guard\nloner.int ; nowhere\n");
    tree.write("scripts_src/town.ssl", "procedure map_enter_p_proc begin load_map(\"MapC.map\", 3); end\n");
    tree.write("scripts_src/guard.ssl", "procedure talk_p_proc begin load_map(0, 555); end\n");
    tree.write("scripts_src/loner.ssl", "procedure use_p_proc begin load_map(\"mapb.map\", 0); end\n");

    auto a = Map::createEmptyMapFile();
    a.header.script_id = 1; // town.int, 1-based in the header
    addExitGrid(a, 100, 1, kOpenHexB, 0);
    writeMapFile(a, base / "maps" / "mapa.map");
    auto b = Map::createEmptyMapFile();
    b.map_scripts[static_cast<int>(MapScript::ScriptType::CRITTER)].push_back(
        MapScript::makeObjectScript(MapScript::ScriptType::CRITTER, 0, 1, 7)); // guard.int, 0-based
    writeMapFile(b, base / "maps" / "mapb.map");
    writeMapFile(Map::createEmptyMapFile(), base / "maps" / "mapc.map");

    auto& resources = tree.mount();
    cli::MapGraphOptions options;
    for (const char* name : { "mapa.map", "mapb.map", "mapc.map" }) {
        options.maps.push_back((base / "maps" / name).string());
    }
    const json root = buildGraph(resources, options);
    const json& scriptEdges = root.at("scriptEdges");

    const json* aToC = findEdge(scriptEdges, "mapa.map", "mapc.map");
    REQUIRE(aToC != nullptr);
    REQUIRE(aToC->at("calls").size() == 1);
    CHECK(aToC->at("calls")[0].at("script") == "town.ssl");
    CHECK(aToC->at("calls")[0].at("procedure") == "map_enter_p_proc");
    CHECK(aToC->at("calls")[0].at("entrance") == 3);
    const json* bToA = findEdge(scriptEdges, "mapb.map", "mapa.map"); // by maps.txt index
    REQUIRE(bToA != nullptr);
    CHECK(bToA->at("calls")[0].at("entrance") == 555);
    CHECK(scriptEdges.size() == 2);

    REQUIRE(root.at("unplacedScriptCalls").size() == 1);
    CHECK(root.at("unplacedScriptCalls")[0].at("script") == "loner.ssl");
    CHECK(root.at("unplacedScriptCalls")[0].at("to") == "mapb.map");

    const json& stats = root.at("stats");
    CHECK(stats.at("scriptsRead") == 3);
    CHECK(stats.at("scriptEdges") == 2);
    // mapb has an exit grid in from mapa; mapa and mapc are entered only by script.
    CHECK(stats.at("noIncomingExceptScript") == json::array({ "mapa.map", "mapc.map" }));

    // The exit-grid layer is untouched, and scripts=false leaves the script layer out.
    CHECK(findEdge(root.at("edges"), "mapa.map", "mapb.map") != nullptr);
    options.scripts = false;
    CHECK_FALSE(buildGraph(resources, options).contains("scriptEdges"));
}
