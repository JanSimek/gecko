#include "cli/MapGraph.h"

#include "cli/MapAnalyzer.h" // collectMapExits, listMapPaths, MapExit
#include "cli/MapLoad.h"     // loadMap
#include "format/map/Map.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    std::string baseName(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    ordered_json orNull(const std::string& s) {
        return s.empty() ? ordered_json(nullptr) : ordered_json(s);
    }

    // A map node: its friendly name, and whether we actually loaded it (vs only seeing it as an exit
    // destination of another map).
    struct NodeInfo {
        std::string displayName;
        bool analysed = false;
    };

    // An aggregated edge between two maps: how many exit hexes form it, the raw destination index, the
    // destination's friendly name, and what kind of target it is (map / worldmap / townmap / unknown —
    // an index with no maps.txt entry).
    struct EdgeInfo {
        int destMap = 0;
        std::string toName;
        std::string kind;
        int exits = 0;
    };

    using Nodes = std::map<std::string, NodeInfo>;
    using Edges = std::map<std::pair<std::string, std::string>, EdgeInfo>;

    // Classify one exit's destination and fold it into the node/edge maps.
    void addExit(const resource::MapNameResolver& names, const std::string& fromFile, const MapExit& exit,
        Nodes& nodes, Edges& edges) {
        std::string toKey;
        std::string kind;
        std::string toName;
        if (exit.destMap == -2) { // ExitGrid::WORLD_MAP_EXIT
            toKey = "<world map>";
            kind = "worldmap";
        } else if (exit.destMap == -1) { // ExitGrid::TOWN_MAP_EXIT
            toKey = "<town map>";
            kind = "townmap";
        } else {
            const std::string toFile = names.fileNameOf(exit.destMap);
            toName = names.displayName(exit.destMap, exit.destElevation);
            if (toFile.empty()) {
                toKey = "<map " + std::to_string(exit.destMap) + ">";
                kind = "unknown"; // a destination index with no maps.txt entry
            } else {
                toKey = toFile;
                kind = "map";
                NodeInfo& toNode = nodes[toFile]; // destinations are nodes too, even if not analysed
                if (toNode.displayName.empty()) {
                    toNode.displayName = toName;
                }
            }
        }
        EdgeInfo& edge = edges[{ fromFile, toKey }];
        edge.destMap = exit.destMap;
        edge.kind = kind;
        if (edge.toName.empty()) {
            edge.toName = toName;
        }
        ++edge.exits;
    }

    // Load each map and fold its exit grids into the node/edge maps; returns how many maps loaded.
    int buildGraph(resource::GameResources& resources, const resource::MapNameResolver& names,
        const std::vector<std::string>& mapPaths, Nodes& nodes, Edges& edges) {
        int analysed = 0;
        for (const auto& mapPath : mapPaths) {
            const std::unique_ptr<Map> map = loadMap(resources, mapPath);
            if (!map) {
                continue; // unreadable maps simply don't appear as source nodes
            }
            const std::string fromFile = baseName(mapPath);
            const int fromIndex = names.indexOf(fromFile);
            NodeInfo& fromNode = nodes[fromFile];
            fromNode.analysed = true;
            if (fromNode.displayName.empty() && fromIndex >= 0) {
                fromNode.displayName = names.displayName(fromIndex, 0);
            }
            ++analysed;
            for (const MapExit& exit : collectMapExits(*map)) {
                addExit(names, fromFile, exit, nodes, edges);
            }
        }
        return analysed;
    }

    // Structural insight: which analysed maps have no outgoing / no incoming *map* edge.
    ordered_json graphStats(const Nodes& nodes, const Edges& edges, int analysed) {
        std::set<std::string> hasOutgoing;
        std::set<std::string> hasIncoming;
        for (const auto& [key, edge] : edges) {
            if (edge.kind == "map") {
                hasOutgoing.insert(key.first);
                hasIncoming.insert(key.second);
            }
        }
        auto deadEnds = ordered_json::array();   // analysed maps with no exit to another map
        auto noIncoming = ordered_json::array(); // analysed maps no other map exits to (entry points / orphans)
        for (const auto& [file, info] : nodes) {
            if (!info.analysed) {
                continue;
            }
            if (!hasOutgoing.contains(file)) {
                deadEnds.push_back(file);
            }
            if (!hasIncoming.contains(file)) {
                noIncoming.push_back(file);
            }
        }
        return { { "maps", analysed }, { "nodes", nodes.size() }, { "edges", edges.size() },
            { "deadEnds", std::move(deadEnds) }, { "noIncoming", std::move(noIncoming) } };
    }

    ordered_json graphToJson(const Nodes& nodes, const Edges& edges, int analysed) {
        auto nodesJson = ordered_json::array();
        for (const auto& [file, info] : nodes) {
            nodesJson.push_back({ { "file", file }, { "displayName", orNull(info.displayName) }, { "analysed", info.analysed } });
        }
        auto edgesJson = ordered_json::array();
        for (const auto& [key, info] : edges) {
            edgesJson.push_back({ { "from", key.first }, { "to", key.second }, { "kind", info.kind },
                { "destMap", info.destMap }, { "toName", orNull(info.toName) }, { "exits", info.exits } });
        }
        ordered_json root;
        root["nodes"] = std::move(nodesJson);
        root["edges"] = std::move(edgesJson);
        root["stats"] = graphStats(nodes, edges, analysed);
        return root;
    }

} // namespace

int buildMapGraph(resource::GameResources& resources, const MapGraphOptions& options, std::ostream& out) {
    const resource::MapNameResolver names(resources);

    std::vector<std::string> mapPaths = options.maps.empty() ? listMapPaths(resources.files()) : options.maps;
    std::ranges::sort(mapPaths);
    if (mapPaths.empty()) {
        out << "{\"nodes\":[],\"edges\":[],\"stats\":{\"maps\":0,\"nodes\":0,\"edges\":0}}\n";
        return 1;
    }

    Nodes nodes;
    Edges edges;
    const int analysed = buildGraph(resources, names, mapPaths, nodes, edges);
    out << graphToJson(nodes, edges, analysed).dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
