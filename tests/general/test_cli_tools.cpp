#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <ostream>
#include <sstream>
#include <string>

#include "cli/HexSightline.h"
#include "cli/MapDescribe.h"
#include "cli/MapExport.h"
#include "cli/PatternExtract.h"
#include "cli/ScriptIntrospect.h"
#include "mcp/McpServer.h"
#include "resource/GameResources.h"

#include "support/CliJson.h"
#include "support/MapFileIo.h"
#include "support/McpCall.h"
#include "support/ShopMap.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;

// Smoke tests for the CLI tools the MCP server exposes, run on test::ShopMap written to disk. Its
// critters and walls need no proto to read back, so the tools run with no game data mounted.
namespace {

using test::ShopMap;

void writeTestMap(const std::filesystem::path& path) {
    test::writeMapFile(ShopMap::build(), path);
}

} // namespace

TEST_CASE("describe_map reports a map read off disk, reachability included", "[cli][mcp]") {
    test::TempFile map{ "geck_cli_describe", ".map" };
    writeTestMap(map.path());
    resource::GameResources resources;

    const json result = test::runJson([&](std::ostream& out) { return cli::describeMap(resources, { map.path().string() }, out); });
    CHECK(result.contains("reachability"));
}

TEST_CASE("export_entities lists the critters of a map", "[cli][mcp]") {
    test::TempFile map{ "geck_cli_export", ".map" };
    writeTestMap(map.path());
    resource::GameResources resources;

    cli::ExportOptions options;
    options.maps = { map.path().string() };
    const json result = test::runJson([&](std::ostream& out) { return cli::exportEntities(resources, options, out); });
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
    options.fromHex = ShopMap::SHOPKEEPER_HEX;
    options.toHex = ShopMap::OTHER_CRITTER_HEX;
    options.mapPath = map.path().string();
    const json result = test::runJson([&](std::ostream& out) { return cli::analyzeHexSightline(resources, options, out); });
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
    options.pids = { ShopMap::CRITTER_PID };
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
    const json result = test::runJson([&](std::ostream& out) { return cli::findText(resources, options, out); });
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

    const json grid = test::mcpToolJson(server, "dump_grid", { { "map", map.path().string() }, { "elevation", 0 } });
    CHECK(grid.at("elevations").at(0).at("objects").size() == ShopMap::OBJECT_COUNT);

    const json reach = test::mcpCall(server, "reachability", { { "map", map.path().string() } });
    CHECK(reach["result"]["isError"] == false);
}
