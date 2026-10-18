#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include "cli/MapExamples.h"
#include "editor/HexGeometry.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "format/pro/ProtoTypes.h"
#include "resource/GameResources.h"
#include "writer/map/MapWriter.h"

#include "support/ProStubProvider.h"
#include "support/DataTree.h"

using nlohmann::json;
using namespace geck;
namespace fs = std::filesystem;

// find_examples on a data directory holding one map: a scripted shopkeeper with a wall beside it and a
// second critter four hexes east. Critters and walls read back without protos, so no game data is
// needed; proto names stay empty, so the filters exercised here are kind, script, scriptCalls, near and maps.
namespace {

constexpr uint32_t CRITTER_PID = 0x01000004;
constexpr int SHOPKEEPER_HEX = 20100;
constexpr int WALL_HEX = 20101;
constexpr int OTHER_CRITTER_HEX = 20104;
constexpr int FAR_WALL_HEX = 30000;
constexpr int SHOPKEEPER_PROGRAM = 1; // scripts.lst line 2, shopkpr.int

void writeShopMap(const fs::path& path) {
    auto mapFile = Map::createEmptyMapFile();
    auto add = [&mapFile](uint32_t pid, int hex, int32_t sid) {
        auto object = std::make_shared<MapObject>();
        object->pro_pid = pid;
        object->position = hex;
        object->elevation = 0;
        object->map_scripts_pid = sid;
        object->script_id = -1;
        mapFile.map_objects[0].push_back(object);
    };
    const uint32_t sid = MapScript::makeSid(MapScript::ScriptType::CRITTER, 0);
    MapScript script{};
    script.pid = sid;
    script.script_id = SHOPKEEPER_PROGRAM;
    mapFile.map_scripts[static_cast<int>(MapScript::ScriptType::CRITTER)].push_back(script);

    add(CRITTER_PID, SHOPKEEPER_HEX, static_cast<int32_t>(sid));
    add(CRITTER_PID, OTHER_CRITTER_HEX, -1);
    add(ProtoId(ObjectType::Wall, 1).pid(), WALL_HEX, -1);
    add(ProtoId(ObjectType::Wall, 1).pid(), FAR_WALL_HEX, -1);

    fs::create_directories(path.parent_path());
    test::StubProvider provider;
    MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
    writer.openFile(path);
    REQUIRE(writer.write(mapFile));
}

json run(resource::GameResources& resources, const cli::ExamplesOptions& options, int& rc) {
    std::ostringstream out;
    rc = cli::findExamples(resources, options, out);
    INFO("tool output: " << out.str());
    return rc == 0 ? json::parse(out.str()) : json();
}

} // namespace

TEST_CASE("hexgrid::distance counts hex steps", "[hex_geometry]") {
    CHECK(hexgrid::distance(SHOPKEEPER_HEX, SHOPKEEPER_HEX) == 0);
    CHECK(hexgrid::distance(SHOPKEEPER_HEX, WALL_HEX) == 1);
    CHECK(hexgrid::distance(SHOPKEEPER_HEX, OTHER_CRITTER_HEX) == 4);
    CHECK(hexgrid::distance(OTHER_CRITTER_HEX, SHOPKEEPER_HEX) == 4);
}

TEST_CASE("find_examples reports matching objects with their script and surroundings", "[cli][examples]") {
    test::DataTree tree{ "map_examples_data" };
    const fs::path& root = tree.root();
    writeShopMap(root / "maps" / "shop.map");
    tree.write("scripts/scripts.lst", "first.int ; First\nshopkpr.int ; Shopkeeper\n");
    tree.write("scripts_src/shopkpr.ssl", "procedure talk_p_proc begin\n   gdialog_mod_barter(0);\nend\n");

    auto& resources = tree.mount();
    int rc = -1;

    SECTION("every critter, nearest neighbours first") {
        cli::ExamplesOptions options;
        options.kind = "critter";
        const json result = run(resources, options, rc);
        REQUIRE(rc == 0);
        CHECK(result["total"] == 2);
        CHECK(result["byMap"][0]["map"] == "shop.map");
        CHECK(result["byMap"][0]["count"] == 2);
        const json& shopkeeper = result["matches"][0];
        CHECK(shopkeeper["hex"] == SHOPKEEPER_HEX);
        CHECK(shopkeeper["script"]["name"] == "shopkpr");
        REQUIRE(shopkeeper["neighbours"].size() == 1); // the wall; the other critter is 4 hexes away
        CHECK(shopkeeper["neighbours"][0]["kind"] == "wall");
        CHECK(shopkeeper["neighbours"][0]["nearest"] == 1);
    }

    SECTION("a script filter matches the script's description; a wider radius reaches the other critter") {
        cli::ExamplesOptions options;
        options.script = "SHOPKEEPER";
        options.contextRadius = 4;
        const json result = run(resources, options, rc);
        REQUIRE(rc == 0);
        REQUIRE(result["total"] == 1);
        const json& neighbours = result["matches"][0]["neighbours"];
        REQUIRE(neighbours.size() == 2);
        CHECK(neighbours[1]["kind"] == "critter");
        CHECK(neighbours[1]["nearest"] == 4);
    }

    SECTION("scriptCalls keeps the objects whose script calls the opcode") {
        cli::ExamplesOptions options;
        options.scriptCalls = "gdialog_mod_barter";
        const json result = run(resources, options, rc);
        REQUIRE(rc == 0);
        REQUIRE(result["total"] == 1);
        CHECK(result["matches"][0]["hex"] == SHOPKEEPER_HEX);

        options.scriptCalls = "gdialog_mod_free";
        CHECK(run(resources, options, rc)["total"] == 0);
    }

    SECTION("near, maps and limit narrow the matches") {
        cli::ExamplesOptions options;
        options.kind = "critter";
        options.nearName = "no such proto";
        CHECK(run(resources, options, rc)["total"] == 0);

        options.nearName.clear();
        options.maps = { "elsewhere" };
        CHECK(run(resources, options, rc)["total"] == 0);
        options.maps = { "SHOP" };
        CHECK(run(resources, options, rc)["total"] == 2);

        options.limit = 1;
        const json limited = run(resources, options, rc);
        CHECK(limited["returned"] == 1);
        CHECK(limited["truncated"] == true);
    }

    SECTION("a query needs at least one filter") {
        run(resources, cli::ExamplesOptions{}, rc);
        CHECK(rc != 0);
    }
}
