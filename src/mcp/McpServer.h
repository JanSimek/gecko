#pragma once

#include <nlohmann/json_fwd.hpp>

namespace geck::resource {
class GameResources;
}

namespace geck::mcp {

/// A minimal Model Context Protocol server over the headless gecko_cli logic: it exposes the
/// analyze/generate tools (plus list_maps / proto_info) to an AI agent as JSON-RPC tools, so a map
/// can be inspected and generated conversationally.
///
/// handleMessage() is pure and never throws — failures become JSON-RPC errors or tool `isError`
/// results — so it is unit-testable without any transport. The gecko-mcp executable is just the
/// stdio frontend that pumps newline-delimited messages through it.
class McpServer {
public:
    explicit McpServer(resource::GameResources& resources);

    /// Handle one JSON-RPC 2.0 request. Returns the response object, or a null json for
    /// notifications (which take no response).
    nlohmann::json handleMessage(const nlohmann::json& request);

private:
    resource::GameResources& _resources;
};

} // namespace geck::mcp
