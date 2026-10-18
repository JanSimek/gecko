#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "mcp/Base64.h"
#include "mcp/McpServer.h"
#include "resource/GameResources.h"

#include "support/McpCall.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;

// The MCP dispatch is pure (no transport, no data needed), so the protocol contract is unit-tested
// directly: requests in, JSON-RPC responses out. The tools themselves reuse the analyze/generate
// logic exercised elsewhere.
TEST_CASE("McpServer speaks JSON-RPC and exposes the tools", "[mcp]") {
    resource::GameResources resources; // no data mounted
    mcp::McpServer server(resources);

    SECTION("initialize advertises the server and its tool capability") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "initialize" } });
        CHECK(resp["id"] == 1);
        CHECK(resp["result"]["serverInfo"]["name"] == "gecko-mcp");
        CHECK(resp["result"].contains("protocolVersion"));
        CHECK(resp["result"]["capabilities"].contains("tools"));
    }

    SECTION("tools/list returns the tools with input schemas") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "tools/list" } });
        std::vector<std::string> names;
        for (const auto& tool : resp["result"]["tools"]) {
            names.push_back(tool["name"].get<std::string>());
            CHECK(tool.contains("inputSchema"));
        }
        for (const char* expected : { "list_maps", "analyze", "palette", "proto_info", "mounts", "describe_script", "find_script", "find_text", "export_entities", "export_protos", "reachability", "describe_map", "map_graph", "world_map", "world_encounters", "quests", "gvars", "endings", "party", "find_examples", "find_gvar", "script_refs", "lint_script", "generate", "render_map", "extract_pattern", "script_api", "frm_info", "resolve_fid", "list_frms", "render_frm", "resource_find", "resource_list", "resource_missing", "describe_save", "hex_sightline" }) {
            CHECK(std::find(names.begin(), names.end(), expected) != names.end());
        }
    }

    SECTION("tools/list annotates every tool; only the file-writing four are not read-only") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "tools/list" } });
        const std::vector<std::string> mutating{ "generate", "render_map", "render_frm", "extract_pattern" };
        for (const auto& tool : resp["result"]["tools"]) {
            REQUIRE(tool.contains("annotations"));
            const auto& hints = tool["annotations"];
            const auto name = tool["name"].get<std::string>();
            const bool writes = std::ranges::find(mutating, name) != mutating.end();
            CHECK(hints["readOnlyHint"] == !writes);
            CHECK(hints["destructiveHint"] == false); // the mutating tools only write new files
            CHECK(hints["openWorldHint"] == false);   // everything operates on local data
        }
    }

    SECTION("tools/list advertises the embed flag on both render tools") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "tools/list" } });
        int seen = 0;
        for (const auto& tool : resp["result"]["tools"]) {
            const auto name = tool["name"].get<std::string>();
            if (name == "render_map" || name == "render_frm") {
                CHECK(tool["inputSchema"]["properties"].contains("embed"));
                ++seen;
            }
        }
        CHECK(seen == 2);
    }

    // Every tool called without its required arguments, or with no data mounted, answers with a tool
    // error (not a crash and not a protocol error) that carries no structuredContent.
    SECTION("a tool missing its arguments or its data reports a tool error") {
        struct Row {
            const char* tool;
            json arguments;
            const char* fragment; // a substring the error text must contain
        };
        const Row rows[] = {
            // The 0-based programIndex is the engine's own index base (see cli/ScriptIntrospect.h), so
            // the 1-based SCRIPT_* constants from headers/scripts.h name the NEXT script if passed
            // unadjusted. Defaulting a missing selector to 0 would describe obj_dude just as silently,
            // so both script-selecting tools insist on one and say which base they mean.
            { "describe_script", json::object(), "1-based" },
            { "find_script", json::object(), "1-based" },
            // programIndex 0 is accepted: with no data it fails only for the missing scripts.lst.
            { "describe_script", { { "programIndex", 0 } }, "scripts.lst not found" },
            // Scenery and walls are ~98% of every map's object records and nobody searches for a wall,
            // so the export omits them unless asked. With no data mounted the call still answers cleanly.
            { "export_entities", json::object(), "no maps" },
            { "find_text", json::object(), "'pattern'" },
            { "analyze", json::object(), "No .map files" },
            { "proto_info", json::object(), "'pid'" },
            { "resource_find", json::object(), "'path'" },
            { "render_map", json::object(), "'map'" },
            { "reachability", json::object(), "'map'" },
            { "describe_map", json::object(), "'map'" },
            { "world_encounters", { { "x", 10 } }, "'x' and 'y'" },
            // The data-reading tools print their empty result and still flag the missing data.
            { "map_graph", json::object(), "" },
            { "world_map", json::object(), "" },
            { "world_encounters", json::object(), "" },
            { "quests", json::object(), "" },
            { "gvars", json::object(), "" },
            { "endings", json::object(), "" },
            { "party", json::object(), "" },
            { "resolve_fid", { { "fid", "nope" } }, "not a valid FID" },
            { "frm_info", json::object(), "'frm'" },
            { "render_frm", { { "frm", "ext2grd1" } }, "'out'" },
            { "list_frms", json::object(), "'glob'" },
        };
        for (const auto& row : rows) {
            INFO(row.tool << " " << row.arguments.dump());
            const json resp = test::mcpCall(server, row.tool, row.arguments);
            REQUIRE(resp.contains("result"));
            CHECK(resp["result"]["isError"] == true);
            CHECK_FALSE(resp["result"].contains("structuredContent"));
            const auto text = resp["result"]["content"][0]["text"].get<std::string>();
            CHECK(text.find(row.fragment) != std::string::npos);
        }
    }

    SECTION("export_protos reports missing data and rejects filters it cannot honour") {
        const json noData = test::mcpCall(server, "export_protos");
        CHECK(noData["result"]["isError"] == true);
        CHECK(noData["result"]["content"][0]["text"].get<std::string>().find("items.lst") != std::string::npos);

        for (json bad : { json{ { "kind", "robot" } }, json{ { "itemType", "gun" } },
                 json{ { "kind", "critter" }, { "itemType", "weapon" } }, json{ { "kind", 5 } } }) {
            const json resp = test::mcpCall(server, "export_protos", bad);
            CHECK(resp["result"]["isError"] == true);
            // A rejected filter never reaches the data: the message is about the argument.
            CHECK(resp["result"]["content"][0]["text"].get<std::string>().find("items.lst") == std::string::npos);
        }
    }

    SECTION("a notification (no id) gets no response") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } });
        CHECK(resp.is_null());
    }

    SECTION("an unknown method is a -32601 error") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 3 }, { "method", "no/such" } });
        CHECK(resp["error"]["code"] == -32601);
    }

    SECTION("an unknown tool is a -32602 error") {
        const json resp = test::mcpCall(server, "nope");
        CHECK(resp["error"]["code"] == -32602);
    }

    SECTION("list_maps with no data mounted is an empty list, not an error") {
        const json resp = test::mcpCall(server, "list_maps");
        CHECK(resp["result"]["isError"] == false);
        CHECK(resp["result"]["content"][0]["text"] == "[]");
        // An array-emitting tool wraps its structuredContent in an object (the MCP schema
        // requires an object), under a key named for the payload.
        CHECK(resp["result"]["structuredContent"] == json{ { "maps", json::array() } });
    }

    SECTION("a JSON-object tool carries structuredContent mirroring its text block") {
        const json resp = test::mcpCall(server, "resolve_fid", { { "fid", "0x05000021" } });
        REQUIRE(resp["result"]["isError"] == false);
        REQUIRE(resp["result"].contains("structuredContent"));
        const json text = json::parse(resp["result"]["content"][0]["text"].get<std::string>());
        CHECK(resp["result"]["structuredContent"] == text);
        CHECK(resp["result"]["structuredContent"]["type"] == "misc");
    }

    SECTION("a plain-text tool result has no structuredContent") {
        const json resp = test::mcpCall(server, "script_api");
        CHECK(resp["result"]["isError"] == false);
        CHECK_FALSE(resp["result"].contains("structuredContent"));
    }

    SECTION("resource_find with no data mounted reports found=false, not an error") {
        const json resp = test::mcpCall(server, "resource_find", { { "path", "art/tiles/gras030.frm" } });
        CHECK(resp["result"]["isError"] == false);
        const json payload = json::parse(resp["result"]["content"][0]["text"].get<std::string>());
        CHECK(payload["found"] == false);
        CHECK(payload["source"].is_null());
    }

    SECTION("list_frms with no data mounted is an empty array, not an error") {
        const json resp = test::mcpCall(server, "list_frms", { { "glob", "ext2grd*" } });
        CHECK(resp["result"]["isError"] == false);
        CHECK(resp["result"]["content"][0]["text"] == "[]\n");
        CHECK(resp["result"]["structuredContent"] == json{ { "frms", json::array() } });
    }

    SECTION("bad argument types and out-of-range numbers are tool errors, not silent casts") {
        // A negative pid would have wrapped to a huge uint32; a string pid was silently ignored.
        CHECK(test::mcpCall(server, "proto_info", { { "pid", -5 } })["result"]["isError"] == true);
        CHECK(test::mcpCall(server, "proto_info", { { "pid", "nope" } })["result"]["isError"] == true);
        // A negative maxDimension would have become an enormous unsigned value.
        CHECK(test::mcpCall(server, "render_map", { { "map", "m.map" }, { "out", "o.png" }, { "maxDimension", -1 } })["result"]["isError"] == true);
        // A non-string required arg used to slip through as an empty string.
        CHECK(test::mcpCall(server, "render_map", { { "map", 123 }, { "out", "o.png" } })["result"]["isError"] == true);
        // embed must be a real boolean, not a truthy string.
        CHECK(test::mcpCall(server, "render_map", { { "map", "m.map" }, { "out", "o.png" }, { "embed", "yes" } })["result"]["isError"] == true);
        CHECK(test::mcpCall(server, "render_frm", { { "frm", "ext2grd1" }, { "out", "o.png" }, { "embed", 1 } })["result"]["isError"] == true);
        // A negative entry in a pid array would have wrapped too.
        CHECK(test::mcpCall(server, "extract_pattern", { { "map", "m" }, { "out", "o" }, { "name", "n" }, { "pids", json::array({ -1 }) } })["result"]["isError"] == true);
        // find_gvar requires a non-empty gvar name (empty would match every line).
        CHECK(test::mcpCall(server, "find_gvar")["result"]["isError"] == true);
        // dump_grid used to ignore a mistyped flag and dump with the default instead.
        CHECK(test::mcpCall(server, "dump_grid", { { "map", "m.map" }, { "elevation", "1" } })["result"]["isError"] == true);
        CHECK(test::mcpCall(server, "dump_grid", { { "map", "m.map" }, { "roof", "true" } })["result"]["isError"] == true);
    }

    SECTION("ping is answered promptly with an empty result") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 30 }, { "method", "ping" } });
        CHECK(resp["id"] == 30);
        CHECK(resp["result"].is_object());
        CHECK(!resp.contains("error"));
    }

    SECTION("a request method sent as a notification (no id) does not run and gets no response") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "method", "tools/call" },
            { "params", { { "name", "list_maps" }, { "arguments", json::object() } } } });
        CHECK(resp.is_null());
    }

    SECTION("a request that isn't JSON-RPC 2.0 is rejected with -32600") {
        const json resp = server.handleMessage({ { "id", 31 }, { "method", "tools/list" } });
        CHECK(resp["error"]["code"] == -32600);
    }
}

#ifdef GECK_SCRIPTING_ENABLED
TEST_CASE("McpServer generate stops a runaway script and stays responsive", "[mcp][generate]") {
    resource::GameResources resources;
    mcp::McpServer server(resources);
    geck::test::TempFile script{ "geck_mcp_loop", ".luau" };
    geck::test::TempFile out{ "geck_mcp_loop_out", ".map" };
    geck::test::writeFile(script.path(), "while true do end");

    const json resp = test::mcpCall(server, "generate",
        { { "script", script.path().string() }, { "out", out.path().string() }, { "timeBudgetSeconds", 1 } });
    REQUIRE(resp["result"]["isError"] == true);
    CHECK(resp["result"]["content"][0]["text"].get<std::string>().find("time budget") != std::string::npos);

    const json ping = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 41 }, { "method", "ping" } });
    CHECK(ping["id"] == 41);
}

TEST_CASE("McpServer returns a tool's exception as a tool error, not a protocol error", "[mcp]") {
    resource::GameResources resources;
    mcp::McpServer server(resources);
    geck::test::TempFile script{ "geck_mcp_throw", ".luau" };
    geck::test::writeFile(script.path(), "api:paintFloor(0, 271)");

    // The out path names a directory, which the map writer cannot open as a file and throws.
    const json resp = test::mcpCall(server, "generate",
        { { "script", script.path().string() }, { "out", script.path().parent_path().string() } });
    CHECK_FALSE(resp.contains("error"));
    REQUIRE(resp["result"]["isError"] == true);
    CHECK(resp["result"]["content"][0]["text"].get<std::string>().starts_with("generate failed: "));
}
#endif

// The base64 encoder feeds MCP image content blocks; padding and binary bytes must match RFC 4648.
TEST_CASE("encodeBase64 produces RFC 4648 output with padding", "[mcp]") {
    const auto encode = [](std::string_view text) {
        return mcp::encodeBase64(reinterpret_cast<const unsigned char*>(text.data()), text.size());
    };
    CHECK(encode("").empty());
    CHECK(encode("f") == "Zg==");
    CHECK(encode("fo") == "Zm8=");
    CHECK(encode("foo") == "Zm9v");
    CHECK(encode("foob") == "Zm9vYg==");
    CHECK(encode("fooba") == "Zm9vYmE=");
    CHECK(encode("foobar") == "Zm9vYmFy");

    const unsigned char binary[] = { 0x00, 0xFF, 0x10 };
    CHECK(mcp::encodeBase64(binary, sizeof(binary)) == "AP8Q");
}
