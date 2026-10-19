#pragma once

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>

#include "mcp/McpServer.h"

namespace geck::test {

/// The JSON-RPC response to a `tools/call` of `tool` with `arguments`.
inline nlohmann::json mcpCall(mcp::McpServer& server, std::string_view tool,
    nlohmann::json arguments = nlohmann::json::object(), int id = 1) {
    return server.handleMessage({ { "jsonrpc", "2.0" }, { "id", id }, { "method", "tools/call" },
        { "params", { { "name", std::string(tool) }, { "arguments", std::move(arguments) } } } });
}

/// The JSON a successful tool call returned, parsed from its first text content block. Fails the
/// test if the call is a protocol or tool error.
inline nlohmann::json mcpToolJson(mcp::McpServer& server, std::string_view tool,
    nlohmann::json arguments = nlohmann::json::object()) {
    const nlohmann::json response = mcpCall(server, tool, std::move(arguments));
    INFO("response: " << response.dump());
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"]["isError"] == false);
    return nlohmann::json::parse(response["result"]["content"][0]["text"].get<std::string>());
}

} // namespace geck::test
