#include "mcp/McpServer.h"

#include "cli/MapAnalyzer.h"
#include "cli/MapGenerator.h"
#include "cli/MapRender.h"
#include "cli/PatternExtract.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
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

    // --- typed argument access (keeps the tool bodies flat) ----------------------
    std::string optString(const json& args, const char* key) {
        const auto it = args.find(key);
        return it != args.end() && it->is_string() ? it->get<std::string>() : std::string();
    }
    int optInt(const json& args, const char* key, int fallback) {
        const auto it = args.find(key);
        return it != args.end() && it->is_number_integer() ? it->get<int>() : fallback;
    }

    // --- tools -------------------------------------------------------------------
    json toolListMaps(resource::GameResources& resources) {
        std::vector<std::string> paths;
        try {
            for (const auto& path : resources.files().list("*")) {
                std::string ext = path.extension().string();
                std::ranges::transform(ext, ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".map") {
                    paths.push_back(path.generic_string());
                }
            }
            std::ranges::sort(paths);
        } catch (const std::exception&) {
            // no data mounted -> an empty list is a valid answer (mirrors api:listMaps())
        }
        return toolText(json(paths).dump()); // json(vector<string>) -> a JSON array, "[]" when empty
    }

    // analyze (full JSON report) and palette (just the weighted generation palette) share the maps
    // parsing and the same headless analyzeMaps entry.
    json runAnalyze(resource::GameResources& resources, const json& args, bool paletteOnly) {
        cli::AnalyzeOptions opts;
        opts.json = !paletteOnly;
        opts.palette = paletteOnly;
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

    json toolAnalyze(resource::GameResources& resources, const json& args) {
        return runAnalyze(resources, args, /*paletteOnly*/ false);
    }

    json toolPalette(resource::GameResources& resources, const json& args) {
        return runAnalyze(resources, args, /*paletteOnly*/ true);
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
        opts.scriptPath = optString(args, "script");
        opts.outPath = optString(args, "out");
        opts.elevation = optInt(args, "elevation", 0);
        if (opts.scriptPath.empty() || opts.outPath.empty()) {
            return toolText("generate requires 'script' and 'out' string arguments", true);
        }
        if (const auto it = args.find("args"); it != args.end() && it->is_object()) {
            for (const auto& [key, value] : it->items()) {
                opts.args[key] = value.is_string() ? value.get<std::string>() : value.dump();
            }
        }
        if (const auto it = args.find("stamps"); it != args.end() && it->is_object()) {
            for (const auto& [key, value] : it->items()) {
                if (value.is_string()) {
                    opts.stamps[key] = value.get<std::string>();
                }
            }
        }
        std::ostringstream oss;
        const int rc = cli::generateMap(resources, opts, oss);
        return toolText(oss.str(), rc != 0);
    }

    json toolRender(resource::GameResources& resources, const json& args) {
        cli::RenderOptions opts;
        opts.mapPath = optString(args, "map");
        opts.outPath = optString(args, "out");
        opts.elevation = optInt(args, "elevation", opts.elevation);
        opts.maxDimension = static_cast<unsigned int>(optInt(args, "maxDimension", static_cast<int>(opts.maxDimension)));
        if (const auto it = args.find("showRoof"); it != args.end() && it->is_boolean()) {
            opts.showRoof = it->get<bool>();
        }
        if (const auto it = args.find("schematic"); it != args.end() && it->is_boolean()) {
            opts.schematic = it->get<bool>();
        }
        if (const auto it = args.find("objects"); it != args.end() && it->is_boolean()) {
            opts.objects = it->get<bool>();
        }
        if (const auto it = args.find("showBlockers"); it != args.end() && it->is_boolean()) {
            opts.showBlockers = it->get<bool>();
        }
        if (opts.mapPath.empty() || opts.outPath.empty()) {
            return toolText("render_map requires 'map' and 'out' string arguments", true);
        }
        std::ostringstream oss;
        const int rc = cli::renderMap(resources, opts, oss);
        return toolText(oss.str(), rc != 0); // rc != 0 e.g. unreadable map or no GL context
    }

    json toolExtractPattern(resource::GameResources& resources, const json& args) {
        cli::ExtractOptions opts;
        opts.mapPath = optString(args, "map");
        opts.outPath = optString(args, "out");
        opts.name = optString(args, "name");
        opts.elevation = optInt(args, "elevation", opts.elevation);
        opts.anchorHex = optInt(args, "anchorHex", opts.anchorHex);
        opts.radius = optInt(args, "radius", opts.radius);
        if (const auto it = args.find("includeFloor"); it != args.end() && it->is_boolean()) {
            opts.includeFloor = it->get<bool>();
        }
        if (const auto it = args.find("pids"); it != args.end() && it->is_array()) {
            for (const auto& pid : *it) {
                if (pid.is_number_integer()) {
                    opts.pids.push_back(static_cast<uint32_t>(pid.get<int64_t>()));
                }
            }
        }
        if (opts.mapPath.empty() || opts.outPath.empty() || opts.name.empty()) {
            return toolText("extract_pattern requires 'map', 'out' and 'name'", true);
        }
        if (opts.pids.empty() && opts.anchorHex < 0) {
            return toolText("extract_pattern needs 'pids' (proto PIDs locating the structure) or 'anchorHex'", true);
        }
        std::ostringstream oss;
        const int rc = cli::extractPattern(resources, opts, oss);
        return toolText(oss.str(), rc != 0);
    }

    // Dispatch a tools/call by name. Returns the tool result, or nullopt for an unknown tool
    // (which the caller turns into a JSON-RPC method error).
    std::optional<json> callTool(resource::GameResources& resources, const std::string& name, const json& args) {
        if (name == "list_maps") {
            return toolListMaps(resources);
        }
        if (name == "analyze") {
            return toolAnalyze(resources, args);
        }
        if (name == "palette") {
            return toolPalette(resources, args);
        }
        if (name == "proto_info") {
            return toolProtoInfo(resources, args);
        }
        if (name == "generate") {
            return toolGenerate(resources, args);
        }
        if (name == "render_map") {
            return toolRender(resources, args);
        }
        if (name == "extract_pattern") {
            return toolExtractPattern(resources, args);
        }
        return std::nullopt;
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
            { { "name", "palette" },
                { "description", "The weighted generation palette for the given maps (omit 'maps' for all), "
                                 "aggregated: { floor:[{id,name,weight}], scenery:[{pid,number,name,weight}] }. "
                                 "Just what a generator script needs — floor 'id' for api:paintFloor, scenery "
                                 "'number' for api:proto, 'weight' = real placement count — without the full "
                                 "analyze report. scenery is scatter-eligible only (scenery type, non-flat)." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "maps", { { "type", "array" }, { "items", { { "type", "string" } } } } } } } } } },
            { { "name", "proto_info" },
                { "description", "Resolve a proto PID to its type, engine display name and 'flat' flag." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "pid", { { "type", "integer" } } } } }, { "required", json::array({ "pid" }) } } } },
            { { "name", "generate" },
                { "description", "Run a Luau generation script against a fresh map and write a .map. "
                                 "Args: script (path to the .luau), out (filesystem path for the .map — "
                                 "render_map/analyze can read it straight back), optional elevation, optional "
                                 "args (string map), optional stamps (name -> stamp .json path, placed by the "
                                 "script with api:placeStamp(name, anchorHex, variant)). Scripting build required." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "script", { { "type", "string" } } }, { "out", { { "type", "string" } } }, { "elevation", { { "type", "integer" } } }, { "args", { { "type", "object" } } }, { "stamps", { { "type", "object" } } } } }, { "required", json::array({ "script", "out" }) } } } },
            { { "name", "render_map" },
                { "description", "Render a map to a PNG so it can be seen, not just measured. Args: map "
                                 "(.map path), out (output .png path), optional elevation, optional "
                                 "maxDimension (longest side in px, default 1600), optional showRoof, "
                                 "optional schematic. schematic=true flat-colours floor tiles by id "
                                 "and marks objects by category, and returns a colour legend (id/type "
                                 "-> colour -> count) so you can match the picture to the analyze JSON "
                                 "and read the floor-tile transitions. objects=true instead mutes the "
                                 "floor to grey so the category-coloured object markers pop (for checking "
                                 "scatter). FLAT objects (invisible engine blockers) are hidden unless "
                                 "showBlockers. map/out are filesystem paths — out is written there, and map "
                                 "may be a VFS path or any file on disk (e.g. one generate just wrote). Needs "
                                 "an off-screen GL context." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "map", { { "type", "string" } } }, { "out", { { "type", "string" } } }, { "elevation", { { "type", "integer" } } }, { "maxDimension", { { "type", "integer" } } }, { "showRoof", { { "type", "boolean" } } }, { "schematic", { { "type", "boolean" } } }, { "objects", { { "type", "boolean" } } }, { "showBlockers", { { "type", "boolean" } } } } }, { "required", json::array({ "map", "out" }) } } } },
            { { "name", "extract_pattern" },
                { "description", "Capture a structure from a real map into a reusable pattern stamp (JSON the "
                                 "editor's pattern library reads, and generate can place). Locate it with 'pids' "
                                 "(proto PIDs from analyze that make up the structure) — their bounding box, grown "
                                 "by 'radius' (default 2) hexes, is the capture region, so immediate props nearby "
                                 "come along — or pass 'anchorHex' directly. Objects are captured verbatim; pass "
                                 "includeFloor=true to also capture the floor/roof under the region (for structures "
                                 "whose floor is integral). Args: map, out, name, optional elevation, pids (int "
                                 "array), anchorHex, radius, includeFloor." },
                { "inputSchema", { { "type", "object" }, { "properties", { { "map", { { "type", "string" } } }, { "out", { { "type", "string" } } }, { "name", { { "type", "string" } } }, { "elevation", { { "type", "integer" } } }, { "pids", { { "type", "array" }, { "items", { { "type", "integer" } } } } }, { "anchorHex", { { "type", "integer" } } }, { "radius", { { "type", "integer" } } }, { "includeFloor", { { "type", "boolean" } } } } }, { "required", json::array({ "map", "out", "name" }) } } } },
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
            if (auto result = callTool(_resources, name, args)) {
                return resultMessage(id, std::move(*result));
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
