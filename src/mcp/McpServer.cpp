#include "mcp/McpServer.h"

#include "cli/MapAnalyzer.h"
#include "cli/MapGenerator.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace geck::mcp {

using nlohmann::json;

namespace {
    constexpr const char* kProtocolVersion = "2024-11-05";
    constexpr const char* kServerName = "gecko-mcp";
    constexpr const char* kServerVersion = "0.1.0";

    // --- JSON-RPC envelopes ------------------------------------------------------
    json resultMessage(const json& id, json result) {
        return json{ { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move(result) } };
    }
    json errorMessage(const json& id, int code, const std::string& message) {
        return json{ { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
    }
    // An MCP tool result: content blocks (we only emit text) plus an isError flag.
    json toolText(const std::string& text, bool isError = false) {
        return json{ { "content", json::array({ { { "type", "text" }, { "text", text } } }) }, { "isError", isError } };
    }

    // --- tools -------------------------------------------------------------------
    json toolListMaps(resource::GameResources& resources) {
        json maps = json::array();
        try {
            std::vector<std::string> paths;
            for (const auto& path : resources.files().list("*")) {
                std::string ext = path.extension().string();
                std::ranges::transform(ext, ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".map") {
                    paths.push_back(path.generic_string());
                }
            }
            std::ranges::sort(paths);
            for (const auto& p : paths) {
                maps.push_back(p);
            }
        } catch (const std::exception&) {
            // no data mounted -> an empty list is a valid answer (mirrors api:listMaps())
        }
        return toolText(maps.dump());
    }

    json toolAnalyze(resource::GameResources& resources, const json& args) {
        cli::AnalyzeOptions opts;
        opts.json = true;
        if (args.contains("maps") && args["maps"].is_array()) {
            for (const auto& m : args["maps"]) {
                if (m.is_string()) {
                    opts.maps.push_back(m.get<std::string>());
                }
            }
        }
        std::ostringstream oss;
        int rc = 0;
        try {
            rc = cli::analyzeMaps(resources, opts, oss);
        } catch (const std::exception& e) {
            return toolText(std::string("analyze failed: ") + e.what() + " (is the Fallout 2 data mounted?)", true);
        }
        return toolText(oss.str(), rc != 0); // rc != 0 e.g. when no maps are found
    }

    json toolProtoInfo(resource::GameResources& resources, const json& args) {
        if (!args.contains("pid") || !args["pid"].is_number_integer()) {
            return toolText("proto_info requires an integer 'pid' argument", true);
        }
        const auto pid = static_cast<uint32_t>(args["pid"].get<int64_t>());
        try {
            std::string name;
            bool flat = false;
            if (const Pro* pro = resources.repository().load<Pro>(ProHelper::basePath(resources, pid)); pro != nullptr) {
                flat = Pro::hasFlag(pro->header.flags, Pro::ObjectFlags::OBJECT_FLAT);
                if (Msg* msg = ProHelper::msgFile(resources, pro->type()); msg != nullptr) {
                    name = msg->message(pro->header.message_id).text;
                }
            }
            const json info{ { "pid", pid }, { "type", Pro::typeToString(Pro::typeOfPid(pid)) },
                { "name", name }, { "flat", flat } };
            return toolText(info.dump());
        } catch (const std::exception& e) {
            return toolText(std::string("proto_info failed for pid ") + std::to_string(pid) + ": " + e.what(), true);
        }
    }

    json toolGenerate(resource::GameResources& resources, const json& args) {
        cli::GenerateOptions opts;
        opts.scriptPath = args.contains("script") && args["script"].is_string() ? args["script"].get<std::string>() : "";
        opts.outPath = args.contains("out") && args["out"].is_string() ? args["out"].get<std::string>() : "";
        opts.elevation = args.contains("elevation") && args["elevation"].is_number_integer() ? args["elevation"].get<int>() : 0;
        if (opts.scriptPath.empty() || opts.outPath.empty()) {
            return toolText("generate requires 'script' and 'out' string arguments", true);
        }
        if (args.contains("args") && args["args"].is_object()) {
            for (const auto& [key, value] : args["args"].items()) {
                opts.args[key] = value.is_string() ? value.get<std::string>() : value.dump();
            }
        }
        std::ostringstream oss;
        const int rc = cli::generateMap(resources, opts, oss);
        return toolText(oss.str(), rc != 0);
    }

    // Tool schemas advertised by tools/list.
    json toolDefinitions() {
        return json::array({
            { { "name", "list_maps" },
                { "description", "List every .map file in the mounted Fallout 2 data." },
                { "inputSchema", { { "type", "object" }, { "properties", json::object() } } } },
            { { "name", "analyze" },
                { "description", "Analyze ground-tile and object usage as JSON. Omit 'maps' to analyze "
                                 "every map, or pass it to scope. Each object carries a 'flat' flag "
                                 "(structural blocker vs. decoration) for curating a scatter palette." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "maps", { { "type", "array" }, { "items", { { "type", "string" } } } } } } } } } },
            { { "name", "proto_info" },
                { "description", "Resolve a proto PID to its type, engine display name and 'flat' flag." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "pid", { { "type", "integer" } } } } }, { "required", json::array({ "pid" }) } } } },
            { { "name", "generate" },
                { "description", "Run a Luau generation script against a fresh map and write a .map. "
                                 "Args: script, out, optional elevation, optional args (string map). "
                                 "Needs a scripting-enabled build." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "script", { { "type", "string" } } }, { "out", { { "type", "string" } } }, { "elevation", { { "type", "integer" } } }, { "args", { { "type", "object" } } } } }, { "required", json::array({ "script", "out" }) } } } },
        });
    }
} // namespace

McpServer::McpServer(resource::GameResources& resources)
    : _resources(resources) {
}

json McpServer::handleMessage(const json& request) {
    const bool isNotification = !request.contains("id");
    const json id = isNotification ? json(nullptr) : request["id"];
    try {
        const std::string method = request.value("method", "");

        if (method == "initialize") {
            return resultMessage(id, { { "protocolVersion", kProtocolVersion }, { "capabilities", { { "tools", json::object() } } }, { "serverInfo", { { "name", kServerName }, { "version", kServerVersion } } } });
        }
        if (method == "tools/list") {
            return resultMessage(id, { { "tools", toolDefinitions() } });
        }
        if (method == "tools/call") {
            const json params = request.contains("params") ? request["params"] : json::object();
            const std::string name = params.value("name", "");
            const json args = params.contains("arguments") ? params["arguments"] : json::object();
            if (name == "list_maps") {
                return resultMessage(id, toolListMaps(_resources));
            }
            if (name == "analyze") {
                return resultMessage(id, toolAnalyze(_resources, args));
            }
            if (name == "proto_info") {
                return resultMessage(id, toolProtoInfo(_resources, args));
            }
            if (name == "generate") {
                return resultMessage(id, toolGenerate(_resources, args));
            }
            return errorMessage(id, -32602, "Unknown tool: " + name);
        }
        if (isNotification) {
            return json(nullptr); // e.g. notifications/initialized — no response
        }
        return errorMessage(id, -32601, "Method not found: " + method);
    } catch (const std::exception& e) {
        if (isNotification) {
            return json(nullptr);
        }
        return errorMessage(id, -32603, std::string("internal error: ") + e.what());
    }
}

} // namespace geck::mcp
