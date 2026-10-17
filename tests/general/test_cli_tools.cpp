#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include "cli/HexSightline.h"
#include "cli/MapDescribe.h"
#include "cli/MapExport.h"
#include "cli/PatternExtract.h"
#include "cli/ScriptIntrospect.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/ProtoTypes.h"
#include "mcp/McpServer.h"
#include "resource/GameResources.h"
#include "writer/map/MapWriter.h"

#include "support/ProStubProvider.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;

// Smoke tests for the CLI tools the MCP server exposes, run on a map written to disk. Critters,
// walls and exit grids need no proto to read back, so the tools run with no game data mounted.
namespace {

constexpr uint32_t CRITTER_PID = 0x01000004;
constexpr int CRITTER_HEX = 20100;
constexpr int OTHER_CRITTER_HEX = 20104;

void writeTestMap(const std::filesystem::path& path) {
    auto mapFile = Map::createEmptyMapFile();
    auto add = [&](uint32_t pid, int hex) {
        auto object = std::make_shared<MapObject>();
        object->pro_pid = pid;
        object->position = hex;
        object->elevation = 0;
        object->map_scripts_pid = -1;
        object->script_id = -1;
        mapFile.map_objects[0].push_back(object);
    };
    add(CRITTER_PID, CRITTER_HEX);
    add(CRITTER_PID, OTHER_CRITTER_HEX);
    add(ProtoId(ObjectType::Wall, 1).pid(), 20500);

    test::StubProvider provider;
    MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
    writer.openFile(path);
    REQUIRE(writer.write(mapFile));
}

json parseOutput(const std::ostringstream& out) {
    INFO("tool output: " << out.str());
    return json::parse(out.str());
}

} // namespace

TEST_CASE("describe_map reports a map read off disk, reachability included", "[cli][mcp]") {
    test::TempFile map{ "geck_cli_describe", ".map" };
    writeTestMap(map.path());
    resource::GameResources resources;

    std::ostringstream out;
    REQUIRE(cli::describeMap(resources, { map.path().string() }, out) == 0);
    const json result = parseOutput(out);
    CHECK(result.contains("reachability"));
}

TEST_CASE("export_entities lists the critters of a map", "[cli][mcp]") {
    test::TempFile map{ "geck_cli_export", ".map" };
    writeTestMap(map.path());
    resource::GameResources resources;

    cli::ExportOptions options;
    options.maps = { map.path().string() };
    std::ostringstream out;
    REQUIRE(cli::exportEntities(resources, options, out) == 0);
    const json result = parseOutput(out);
    CHECK(result.at("mapsUnreadable").empty());
    int critters = 0;
    for (const auto& entity : result.at("entities")) {
        critters += entity.at("kind") == "critter" ? 1 : 0;
    }
    CHECK(critters == 2);
}

TEST_CASE("hex_sightline walks between two hexes of a map", "[cli][mcp]") {
    test::TempFile map{ "geck_cli_sightline", ".map" };
    writeTestMap(map.path());
    resource::GameResources resources;

    cli::HexSightlineOptions options;
    options.fromHex = CRITTER_HEX;
    options.toHex = OTHER_CRITTER_HEX;
    options.mapPath = map.path().string();
    std::ostringstream out;
    REQUIRE(cli::analyzeHexSightline(resources, options, out) == 0);
    const json result = parseOutput(out);
    CHECK(result.at("distance") == 4);

    options.fromHex = -5; // off the grid
    std::ostringstream bad;
    CHECK(cli::analyzeHexSightline(resources, options, bad) != 0);
}

TEST_CASE("extract_pattern captures the objects matched by PID", "[cli][mcp]") {
    test::TempFile map{ "geck_cli_extract", ".map" };
    test::TempFile stamp{ "geck_cli_extract_stamp", ".json" };
    writeTestMap(map.path());
    resource::GameResources resources;

    cli::ExtractOptions options;
    options.mapPath = map.path().string();
    options.outPath = stamp.path().string();
    options.name = "critters";
    options.pids = { CRITTER_PID };
    options.radius = 0;
    std::ostringstream out;
    REQUIRE(cli::extractPattern(resources, options, out) == 0);
    REQUIRE(std::filesystem::exists(stamp.path()));
    const json pattern = json::parse(test::readAllBytes(stamp.path()));
    INFO(pattern.dump());
    CHECK(pattern.at("name") == "critters");
}

TEST_CASE("find_text with no data mounted finds nothing and says so", "[cli][mcp]") {
    resource::GameResources resources;
    cli::FindTextOptions options;
    options.pattern = "vault";
    std::ostringstream out;
    REQUIRE(cli::findText(resources, options, out) == 0);
    const json result = parseOutput(out);
    CHECK(result.at("matches").empty());

    options.regex = true;
    options.pattern = "(unclosed";
    std::ostringstream bad;
    CHECK(cli::findText(resources, options, bad) != 0);
}

TEST_CASE("McpServer answers data-backed tool calls on a map read off disk", "[mcp]") {
    test::TempFile map{ "geck_mcp_tools_map", ".map" };
    writeTestMap(map.path());
    resource::GameResources resources;
    mcp::McpServer server(resources);
    auto call = [&](const char* tool, json args) {
        return server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "tools/call" },
            { "params", { { "name", tool }, { "arguments", std::move(args) } } } });
    };

    const json grid = call("dump_grid", { { "map", map.path().string() }, { "elevation", 0 } });
    REQUIRE(grid["result"]["isError"] == false);
    const json gridJson = json::parse(grid["result"]["content"][0]["text"].get<std::string>());
    CHECK(gridJson.at("elevations").at(0).at("objects").size() == 3);

    const json reach = call("reachability", { { "map", map.path().string() } });
    CHECK(reach["result"]["isError"] == false);
}
