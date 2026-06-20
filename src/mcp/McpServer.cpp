#include "mcp/McpServer.h"

#include "cli/MapAnalyzer.h"
#include "cli/MapDescribe.h"
#include "cli/MapGenerator.h"
#include "cli/MapReachability.h"
#include "cli/MapRender.h"
#include "cli/PatternExtract.h"
#include "cli/ScriptIntrospect.h"
#include "format/msg/Msg.h"
#include "scripting/ScriptApiReference.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
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

    // --- typed argument access --------------------------------------------------
    // A bad tool argument: thrown by the require*/checked accessors and turned into an isError tool
    // result by callTool, so the handlers can validate as straight-line code.
    struct ToolError {
        std::string message;
    };

    std::string optString(const json& args, const char* key) {
        const auto it = args.find(key);
        return it != args.end() && it->is_string() ? it->get<std::string>() : std::string();
    }
    // A required, non-empty string argument.
    std::string requireString(const json& args, const char* key) {
        const auto it = args.find(key);
        if (it == args.end() || !it->is_string() || it->get<std::string>().empty()) {
            throw ToolError{ std::string("argument '") + key + "' must be a non-empty string" };
        }
        return it->get<std::string>();
    }
    // Optional integer in [min, max]: absent -> fallback (unvalidated), but a present value must be a
    // genuine in-range integer — no silently-ignored wrong type, no negative wrapping to a huge
    // unsigned downstream.
    int64_t optInt(const json& args, const char* key, int64_t fallback, int64_t min, int64_t max) {
        const auto it = args.find(key);
        if (it == args.end()) {
            return fallback;
        }
        if (!it->is_number_integer()) {
            throw ToolError{ std::string("argument '") + key + "' must be an integer" };
        }
        const int64_t value = it->get<int64_t>();
        if (value < min || value > max) {
            throw ToolError{ std::string("argument '") + key + "' must be in ["
                + std::to_string(min) + ", " + std::to_string(max) + "]" };
        }
        return value;
    }
    // A required integer in [min, max].
    int64_t requireInt(const json& args, const char* key, int64_t min, int64_t max) {
        if (args.find(key) == args.end()) {
            throw ToolError{ std::string("argument '") + key + "' is required" };
        }
        return optInt(args, key, 0, min, max);
    }
    bool optBool(const json& args, const char* key, bool fallback) {
        const auto it = args.find(key);
        if (it == args.end()) {
            return fallback;
        }
        if (!it->is_boolean()) {
            throw ToolError{ std::string("argument '") + key + "' must be a boolean" };
        }
        return it->get<bool>();
    }
    // Collect the integer entries of an optional `key` array into `out`, rejecting non-integers and
    // negatives (a negative would wrap to a huge PID).
    void parsePidArray(const json& args, const char* key, std::vector<std::uint32_t>& out) {
        const auto it = args.find(key);
        if (it == args.end()) {
            return;
        }
        if (!it->is_array()) {
            throw ToolError{ std::string("argument '") + key + "' must be an array of non-negative integers" };
        }
        for (const auto& pid : *it) {
            if (!pid.is_number_integer() || pid.get<int64_t>() < 0) {
                throw ToolError{ std::string("argument '") + key + "' must contain only non-negative integers" };
            }
            out.push_back(static_cast<std::uint32_t>(pid.get<int64_t>()));
        }
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

    json toolDescribeScript(resource::GameResources& resources, const json& args) {
        cli::DescribeScriptOptions opts;
        opts.programIndex = static_cast<int>(requireInt(args, "programIndex", 0, INT32_MAX));
        if (const std::string locale = optString(args, "locale"); !locale.empty()) {
            opts.locale = locale;
        }
        std::ostringstream oss;
        const int rc = cli::describeScript(resources, opts, oss);
        return toolText(oss.str(), rc != 0);
    }

    json toolReachability(resource::GameResources& resources, const json& args) {
        cli::ReachabilityOptions opts;
        opts.mapPath = requireString(args, "map");
        std::ostringstream oss;
        int rc = 0;
        try {
            rc = cli::analyzeReachability(resources, opts, oss);
        } catch (const std::exception& e) {
            return toolText(std::string("reachability failed: ") + e.what() + " (is the Fallout 2 data mounted?)", true);
        }
        return toolText(oss.str(), rc != 0);
    }

    json toolDescribeMap(resource::GameResources& resources, const json& args) {
        cli::DescribeMapOptions opts;
        opts.mapPath = requireString(args, "map");
        std::ostringstream oss;
        int rc = 0;
        try {
            rc = cli::describeMap(resources, opts, oss);
        } catch (const std::exception& e) {
            return toolText(std::string("describe_map failed: ") + e.what() + " (is the Fallout 2 data mounted?)", true);
        }
        return toolText(oss.str(), rc != 0);
    }

    json toolProtoInfo(resource::GameResources& resources, const json& args) {
        const auto pid = static_cast<uint32_t>(requireInt(args, "pid", 0, UINT32_MAX));
        try {
            std::string name;
            bool flat = false;
            if (const Pro* pro = resources.loadPro(pid); pro != nullptr) {
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
        opts.scriptPath = requireString(args, "script");
        opts.outPath = requireString(args, "out");
        opts.elevation = static_cast<int>(optInt(args, "elevation", 0, 0, 2));
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
        opts.mapPath = requireString(args, "map");
        opts.outPath = requireString(args, "out");
        opts.elevation = static_cast<int>(optInt(args, "elevation", opts.elevation, 0, 2));
        opts.maxDimension = static_cast<unsigned int>(optInt(args, "maxDimension", opts.maxDimension, 1, 100000));
        opts.showRoof = optBool(args, "showRoof", opts.showRoof);
        opts.schematic = optBool(args, "schematic", opts.schematic);
        opts.objects = optBool(args, "objects", opts.objects);
        opts.semantic = optBool(args, "semantic", opts.semantic);
        opts.showBlockers = optBool(args, "showBlockers", opts.showBlockers);
        std::ostringstream oss;
        const int rc = cli::renderMap(resources, opts, oss);
        return toolText(oss.str(), rc != 0); // rc != 0 e.g. unreadable map or no GL context
    }

    json toolExtractPattern(resource::GameResources& resources, const json& args) {
        cli::ExtractOptions opts;
        opts.mapPath = requireString(args, "map");
        opts.outPath = requireString(args, "out");
        opts.name = requireString(args, "name");
        opts.elevation = static_cast<int>(optInt(args, "elevation", opts.elevation, 0, 2));
        opts.anchorHex = static_cast<int>(optInt(args, "anchorHex", opts.anchorHex, 0, 39999));
        opts.radius = static_cast<int>(optInt(args, "radius", opts.radius, 0, 10000));
        opts.includeFloor = optBool(args, "includeFloor", opts.includeFloor);
        opts.includeRoof = optBool(args, "includeRoof", opts.includeRoof);
        parsePidArray(args, "pids", opts.pids);
        if (opts.pids.empty() && opts.anchorHex < 0) {
            return toolText("extract_pattern needs 'pids' (proto PIDs locating the structure) or 'anchorHex'", true);
        }
        std::ostringstream oss;
        const int rc = cli::extractPattern(resources, opts, oss);
        return toolText(oss.str(), rc != 0);
    }

    json toolScriptApi() {
        return toolText(scriptApiReference());
    }

    // Dispatch a tools/call by name. Returns the tool result, or nullopt for an unknown tool
    // (which the caller turns into a JSON-RPC method error).
    // One contract per tool: the schema advertised by tools/list and the handler run by
    // tools/call live together, so the dispatch and the advertised surface cannot drift. Handlers
    // take (resources, args) uniformly (each ignores what it doesn't need).
    struct ToolSpec {
        std::string name;
        std::string description;
        json inputSchema;
        std::function<json(resource::GameResources&, const json&)> handler;
    };

    const std::vector<ToolSpec>& toolRegistry() {
        static const std::vector<ToolSpec> tools = [] {
            std::vector<ToolSpec> t;
            t.push_back({ "list_maps",
                "List every .map file in the mounted Fallout 2 data.",
                json({ { "type", "object" }, { "properties", json::object() } }),
                [](resource::GameResources& r, const json&) { return toolListMaps(r); } });
            t.push_back({ "analyze",
                "Analyze ground-tile and object usage as JSON. Omit 'maps' to analyze every map, or "
                "pass it to scope. Each object carries a 'flat' flag (structural blocker vs. decoration) "
                "for curating a scatter palette. Each map also lists 'critters': who is on it, their team "
                "(group_id), their AI packet resolved via ai.txt (aggression, disposition, flee/best-weapon/"
                "distance), and the attached 'script' ({programIndex,name}) — pass that programIndex to "
                "describe_script for the script's source and dialog.",
                json({ { "type", "object" }, { "properties", { { "maps", { { "type", "array" }, { "items", { { "type", "string" } } } } } } } }),
                [](resource::GameResources& r, const json& a) { return toolAnalyze(r, a); } });
            t.push_back({ "palette",
                "The weighted generation palette for the given maps (omit 'maps' for all), aggregated: "
                "{ floor:[{id,name,weight}], scenery:[{pid,number,name,weight}] }. Just what a generator "
                "script needs — floor 'id' for api:paintFloor, scenery 'number' for api:proto, 'weight' = "
                "real placement count — without the full analyze report. scenery is scatter-eligible only "
                "(scenery type, non-flat).",
                json({ { "type", "object" }, { "properties", { { "maps", { { "type", "array" }, { "items", { { "type", "string" } } } } } } } }),
                [](resource::GameResources& r, const json& a) { return toolPalette(r, a); } });
            t.push_back({ "proto_info",
                "Resolve a proto PID to its type, engine display name and 'flat' flag.",
                json({ { "type", "object" }, { "properties", { { "pid", { { "type", "integer" } } } } }, { "required", json::array({ "pid" }) } }),
                [](resource::GameResources& r, const json& a) { return toolProtoInfo(r, a); } });
            t.push_back({ "describe_script",
                "Describe a Fallout 2 script by its scripts.lst program index (the 0-based script_id "
                "analyze reports for a critter/object). Returns the filename, the .ssl source if a "
                "script-source tree is mounted (e.g. the FRP scripts_src — hasSource flags whether it was "
                "found), and the dialog .msg lines ([{id,text}]). Lets you read what an NPC does and says. "
                "Optional 'locale' picks the dialog language subdir (default english). Args: programIndex, "
                "optional locale.",
                json({ { "type", "object" }, { "properties", { { "programIndex", { { "type", "integer" } } }, { "locale", { { "type", "string" } } } } }, { "required", json::array({ "programIndex" }) } }),
                [](resource::GameResources& r, const json& a) { return toolDescribeScript(r, a); } });
            t.push_back({ "reachability",
                "Per-elevation reachability for one map. Floods walkable hexes from the entry points "
                "(player start + exit grids — you can arrive at an exit coming from the adjacent map): "
                "'reachableHexes' of 'walkableHexes'; 'orphanedObjects' ([{pid,name,hex}], with "
                "'orphanedObjectCount') are critters/items cut off from every entry — usually a sealed-off "
                "area or a map bug. 'exits' lists each exit grid with 'reachableFromPlayerStart' "
                "(walk-connected to the spawn specifically; null if the player spawns elsewhere). "
                "OPTIMISTIC, not exact pathfinding: doors (incl. locked) are passable, so it over-estimates "
                "rather than crying wolf. Args: map.",
                json({ { "type", "object" }, { "properties", { { "map", { { "type", "string" } } } } }, { "required", json::array({ "map" }) } }),
                [](resource::GameResources& r, const json& a) { return toolReachability(r, a); } });
            t.push_back({ "describe_map",
                "One structured digest for a single map, composed from analyze + reachability: the header "
                "(elevations, darkness, player start, map script, map variables), floor usage (biome), "
                "object 'clusters' (structures), the 'critters' roster with ai.txt-resolved AI and each "
                "one's attached {programIndex,name} script, the 'exits' graph, and a 'reachability' field "
                "(per-elevation walkable/reachable hexes + entry-orphaned objects). Gathers the engine's "
                "own semantic evidence in one call — join keys (pid, script_id, ai_packet) are preserved — "
                "so you can infer the map's purpose and follow up with describe_script on any roster entry. "
                "Args: map.",
                json({ { "type", "object" }, { "properties", { { "map", { { "type", "string" } } } } }, { "required", json::array({ "map" }) } }),
                [](resource::GameResources& r, const json& a) { return toolDescribeMap(r, a); } });
            t.push_back({ "generate",
                "Run a Luau generation script against a fresh map and write a .map. Args: script (path to "
                "the .luau), out (filesystem path for the .map — render_map/analyze can read it straight "
                "back), optional elevation, optional args (string map), optional stamps (name -> stamp "
                ".json path, placed by the script with api:placeStamp(name, anchorHex, variant)). Scripting "
                "build required.",
                json({ { "type", "object" }, { "properties", { { "script", { { "type", "string" } } }, { "out", { { "type", "string" } } }, { "elevation", { { "type", "integer" } } }, { "args", { { "type", "object" } } }, { "stamps", { { "type", "object" } } } } }, { "required", json::array({ "script", "out" }) } }),
                [](resource::GameResources& r, const json& a) { return toolGenerate(r, a); } });
            t.push_back({ "render_map",
                "Render a map to a PNG so it can be seen, not just measured. Args: map (.map path), out "
                "(output .png path), optional elevation, optional maxDimension (longest side in px, default "
                "1600), optional showRoof, optional schematic. schematic=true flat-colours floor tiles by "
                "id and marks objects by category, and returns a colour legend (id/type -> colour -> count) "
                "so you can match the picture to the analyze JSON and read the floor-tile transitions. "
                "objects=true instead mutes the floor to grey so the category-coloured object markers pop "
                "(for checking scatter). semantic=true also greys the floor but colours markers by role — "
                "exit grids highlighted, critters by team, scripted objects ringed (legend keyed by role) — "
                "the purpose layer that pairs with describe_map. FLAT objects (invisible engine blockers) "
                "are hidden unless showBlockers. map/out are filesystem paths — out is written there, and "
                "map may be a VFS path or any file on disk (e.g. one generate just wrote). Needs an "
                "off-screen GL context.",
                json({ { "type", "object" }, { "properties", { { "map", { { "type", "string" } } }, { "out", { { "type", "string" } } }, { "elevation", { { "type", "integer" } } }, { "maxDimension", { { "type", "integer" } } }, { "showRoof", { { "type", "boolean" } } }, { "schematic", { { "type", "boolean" } } }, { "objects", { { "type", "boolean" } } }, { "semantic", { { "type", "boolean" } } }, { "showBlockers", { { "type", "boolean" } } } } }, { "required", json::array({ "map", "out" }) } }),
                [](resource::GameResources& r, const json& a) { return toolRender(r, a); } });
            t.push_back({ "script_api",
                "The generation-script `api` reference (Markdown): every function a `generate` Luau script "
                "can call on the global `api`, with signatures, plus the non-obvious runtime behaviour (runs "
                "are auto-seeded and auto-batched) and the error model. Read this before writing a script "
                "for `generate`.",
                json({ { "type", "object" }, { "properties", json::object() } }),
                [](resource::GameResources&, const json&) { return toolScriptApi(); } });
            t.push_back({ "extract_pattern",
                "Capture a structure from a real map into a reusable pattern stamp (JSON the editor's "
                "pattern library reads, and generate can place). Locate it with 'pids' (proto PIDs from "
                "analyze that make up the structure) — their bounding box, grown by 'radius' (default 2) "
                "hexes, is the capture region, so immediate props nearby come along — or pass 'anchorHex' "
                "directly. Objects are captured verbatim; pass includeFloor=true to capture the ground and "
                "includeRoof=true to capture the roof layer (a tent/building roof is tiles, not an object — "
                "without includeRoof the stamp is topless). Args: map, out, name, optional elevation, pids "
                "(int array), anchorHex, radius, includeFloor, includeRoof.",
                json({ { "type", "object" }, { "properties", { { "map", { { "type", "string" } } }, { "out", { { "type", "string" } } }, { "name", { { "type", "string" } } }, { "elevation", { { "type", "integer" } } }, { "pids", { { "type", "array" }, { "items", { { "type", "integer" } } } } }, { "anchorHex", { { "type", "integer" } } }, { "radius", { { "type", "integer" } } }, { "includeFloor", { { "type", "boolean" } } }, { "includeRoof", { { "type", "boolean" } } } } }, { "required", json::array({ "map", "out", "name" }) } }),
                [](resource::GameResources& r, const json& a) { return toolExtractPattern(r, a); } });
            return t;
        }();
        return tools;
    }

    std::optional<json> callTool(resource::GameResources& resources, const std::string& name, const json& args) {
        const auto& tools = toolRegistry();
        const auto it = std::ranges::find(tools, name, &ToolSpec::name);
        if (it == tools.end()) {
            return std::nullopt;
        }
        try {
            return it->handler(resources, args);
        } catch (const ToolError& e) {
            // Bad arguments are a tool-result error (the call was well-formed; the input wasn't),
            // not a JSON-RPC protocol error.
            return toolText(e.message, /*isError*/ true);
        }
    }

    // Tool schemas advertised by tools/list.
    json toolDefinitions() {
        auto tools = json::array();
        for (const auto& spec : toolRegistry()) {
            tools.push_back({ { "name", spec.name }, { "description", spec.description }, { "inputSchema", spec.inputSchema } });
        }
        return tools;
    }

    // Route a JSON-RPC *request* (already known to have an id and jsonrpc 2.0) to its result. Kept
    // out of McpServer::handleMessage so that stays a thin envelope (notification + version guards +
    // try/catch) and neither function is over-complex.
    json dispatchRequest(resource::GameResources& resources, const json& request, const json& id) {
        const std::string method = request.value("method", "");
        if (method == "initialize") {
            // We speak exactly one protocol version, so negotiation is just declaring it: return
            // kProtocolVersion regardless of what the client asked for, and the client decides whether
            // it can proceed (per the lifecycle spec). Multi-version support would branch here.
            return resultMessage(id, { { "protocolVersion", kProtocolVersion }, { "capabilities", { { "tools", json::object() } } }, { "serverInfo", { { "name", kServerName }, { "version", kServerVersion } } } });
        }
        if (method == "ping") {
            return resultMessage(id, json::object()); // MCP ping -> empty result, answered promptly
        }
        if (method == "tools/list") {
            return resultMessage(id, { { "tools", toolDefinitions() } });
        }
        if (method == "tools/call") {
            const json params = request.contains("params") ? request["params"] : json::object();
            const std::string name = params.value("name", "");
            const json args = params.contains("arguments") ? params["arguments"] : json::object();
            if (auto result = callTool(resources, name, args)) {
                return resultMessage(id, std::move(*result));
            }
            return errorMessage(id, -32602, "Unknown tool: " + name);
        }
        return errorMessage(id, -32601, "Method not found: " + method);
    }
} // namespace

McpServer::McpServer(resource::GameResources& resources)
    : _resources(resources) {
}

json McpServer::handleMessage(const json& request) {
    const bool isNotification = !request.contains("id");
    const json id = isNotification ? json(nullptr) : request["id"];
    try {
        // A notification (no id) takes no response, and we never run a request method (initialize,
        // tools/*, ping) as one — executing a tool with no id to return its result is meaningless. So
        // any no-id message is acknowledged silently regardless of method (e.g. notifications/initialized).
        if (isNotification) {
            return json(nullptr);
        }
        // Past here we owe a response, so the request must declare JSON-RPC 2.0.
        if (request.value("jsonrpc", std::string()) != "2.0") {
            return errorMessage(id, -32600, "Invalid Request: 'jsonrpc' must be \"2.0\"");
        }
        return dispatchRequest(_resources, request, id);
    } catch (const std::exception& e) {
        if (isNotification) {
            return json(nullptr);
        }
        return errorMessage(id, -32603, std::string("internal error: ") + e.what());
    }
}

} // namespace geck::mcp
