#include "cli/MapDescribe.h"

#include <sstream>

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
    AnalyzeOptions analyzeOpts;
    analyzeOpts.json = true;
    analyzeOpts.maps = { options.mapPath };
    std::ostringstream analyzeOut;
    if (analyzeMaps(resources, analyzeOpts, analyzeOut) != 0) {
        out << "describe_map: could not analyse " << options.mapPath
            << " (is it a map under the mounted data?)\n";
        return 1;
    }
    const ordered_json analyzeRoot = ordered_json::parse(analyzeOut.str());
    const auto mapsIt = analyzeRoot.find("maps");
    if (mapsIt == analyzeRoot.end() || !mapsIt->is_array() || mapsIt->empty()) {
        out << "describe_map: no map data for " << options.mapPath << "\n";
        return 1;
    }
    ordered_json digest = mapsIt->front();

    // Cross-reference with reachability: which regions the player can actually walk, and which
    // critters/items are cut off from every entry point. Best-effort — a map with no walkable
    // entry on an elevation still produces a useful digest, so a reachability failure only nulls
    // the field rather than failing the call.
    ReachabilityOptions reachOpts;
    reachOpts.mapPath = options.mapPath;
    std::ostringstream reachOut;
    try {
        if (analyzeReachability(resources, reachOpts, reachOut) == 0) {
            ordered_json reach = ordered_json::parse(reachOut.str());
            // analyzeReachability emits { map, path, reachability: [...per elevation] }; lift the
            // per-elevation array — name/path are already on the digest.
            const auto it = reach.find("reachability");
            digest["reachability"] = (it != reach.end()) ? *it : reach;
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
