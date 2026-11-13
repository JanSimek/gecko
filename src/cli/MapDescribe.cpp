#include "cli/MapDescribe.h"

#include <exception>

#include <nlohmann/json.hpp>

#include "cli/MapAnalyzer.h"
#include "cli/MapReachability.h"

namespace geck::cli {

using ordered_json = nlohmann::ordered_json;

int describeMap(resource::GameResources& resources, const DescribeMapOptions& options, std::ostream& out) {
    if (options.mapPath.empty()) {
        out << "describe_map requires a map path.\n";
        return 2;
    }

    // The per-map analyze digest is the backbone: header (elevations / darkness / player start /
    // map script / map vars), floor usage (biome), object clusters (structures), the critter roster
    // with ai.txt-resolved AI + attached {programIndex, name}, and the exits graph. We run the
    // machine-readable analyze for just this map and lift its single entry.
    ordered_json analyzeRoot = analyzeMapsJson(resources, { options.mapPath });
    ordered_json& maps = analyzeRoot["maps"];
    if (maps.empty()) {
        out << "describe_map: no map data for " << options.mapPath << "\n";
        return 1;
    }
    ordered_json digest = std::move(maps.front());

    // Cross-reference with reachability: which regions the player can actually walk, and which
    // critters/items are cut off from every entry point. Best-effort — a map with no walkable
    // entry on an elevation still produces a useful digest, so a reachability failure only nulls
    // the field rather than failing the call.
    try {
        if (auto reach = reachabilityJson(resources, options.mapPath)) {
            // Lift the per-elevation array — name/path are already on the digest.
            digest["reachability"] = std::move((*reach)["reachability"]);
        } else {
            digest["reachability"] = nullptr;
        }
    } catch (const std::exception&) {
        digest["reachability"] = nullptr;
    }

    // Fallout 2 text (proto/tile/critter names, AI labels) is CP-1252; `replace` keeps dump() from
    // throwing on stray non-UTF-8 bytes (mirrors analyze/reachability).
    out << digest.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
