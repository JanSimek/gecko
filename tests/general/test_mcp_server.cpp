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

    SECTION("tools/list returns the four tools with input schemas") {
        const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "tools/list" } });
        std::vector<std::string> names;
        for (const auto& tool : resp["result"]["tools"]) {
            names.push_back(tool["name"].get<std::string>());
            CHECK(tool.contains("inputSchema"));
        }
        for (const char* expected : { "list_maps", "analyze", "proto_info", "generate" }) {
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
}
