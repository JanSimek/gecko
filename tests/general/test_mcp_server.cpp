#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "mcp/McpServer.h"
#include "resource/GameResources.h"

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
        for (const char* expected : { "list_maps", "analyze", "palette", "proto_info", "describe_script", "reachability", "describe_map", "map_graph", "world_map", "generate", "render_map", "extract_pattern", "script_api" }) {
            CHECK(std::find(names.begin(), names.end(), expected) != names.end());
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
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 4 }, { "method", "tools/call" },
            { "params", { { "name", "nope" }, { "arguments", json::object() } } } });
        CHECK(resp["error"]["code"] == -32602);
    }

    SECTION("list_maps with no data mounted is an empty list, not an error") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 5 }, { "method", "tools/call" },
            { "params", { { "name", "list_maps" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == false);
        CHECK(resp["result"]["content"][0]["text"] == "[]");
    }

    SECTION("analyze with no data mounted reports a tool error (not a crash)") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 6 }, { "method", "tools/call" },
            { "params", { { "name", "analyze" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("proto_info without a pid is a tool error") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 7 }, { "method", "tools/call" },
            { "params", { { "name", "proto_info" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("render_map without map/out is a tool error") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 8 }, { "method", "tools/call" },
            { "params", { { "name", "render_map" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("describe_script without programIndex is a tool error") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 9 }, { "method", "tools/call" },
            { "params", { { "name", "describe_script" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("describe_script accepts index 0 and reports a tool error with no data (not a crash)") {
        // programIndex is 0-based, so 0 must be accepted (not rejected as the old 1-based guard did);
        // with no data mounted it fails only for the missing scripts.lst.
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 10 }, { "method", "tools/call" },
            { "params", { { "name", "describe_script" }, { "arguments", { { "programIndex", 0 } } } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("reachability without a map is a tool error") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 11 }, { "method", "tools/call" },
            { "params", { { "name", "reachability" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("describe_map without a map is a tool error") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 12 }, { "method", "tools/call" },
            { "params", { { "name", "describe_map" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("map_graph with no data mounted reports a tool error (no maps found)") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 13 }, { "method", "tools/call" },
            { "params", { { "name", "map_graph" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("world_map with no data mounted reports a tool error (no city.txt)") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 14 }, { "method", "tools/call" },
            { "params", { { "name", "world_map" }, { "arguments", json::object() } } } });
        CHECK(resp["result"]["isError"] == true);
    }

    SECTION("bad argument types and out-of-range numbers are tool errors, not silent casts") {
        auto call = [&](const char* tool, json toolArgs, int id) {
            return server.handleMessage({ { "jsonrpc", "2.0" }, { "id", id }, { "method", "tools/call" },
                { "params", { { "name", tool }, { "arguments", std::move(toolArgs) } } } });
        };
        // A negative pid would have wrapped to a huge uint32; a string pid was silently ignored.
        CHECK(call("proto_info", { { "pid", -5 } }, 20)["result"]["isError"] == true);
        CHECK(call("proto_info", { { "pid", "nope" } }, 21)["result"]["isError"] == true);
        // A negative maxDimension would have become an enormous unsigned value.
        CHECK(call("render_map", { { "map", "m.map" }, { "out", "o.png" }, { "maxDimension", -1 } }, 22)["result"]["isError"] == true);
        // A non-string required arg used to slip through as an empty string.
        CHECK(call("render_map", { { "map", 123 }, { "out", "o.png" } }, 23)["result"]["isError"] == true);
        // A negative entry in a pid array would have wrapped too.
        CHECK(call("extract_pattern", { { "map", "m" }, { "out", "o" }, { "name", "n" }, { "pids", json::array({ -1 }) } }, 24)["result"]["isError"] == true);
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
