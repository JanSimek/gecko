#include "cli/MapGraph.h"

#include "cli/ConfigLoad.h"
#include "cli/MapAnalyzer.h" // collectMapExits, listMapPaths, MapExit
#include "cli/MapLoad.h"     // loadMap
#include "editor/Reachability.h"
#include "format/city/CityTxt.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "reader/city/CityTxtReader.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
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
        // The individual exit grids forming this edge — the one-way join needs each arrival's
        // destination hex/elevation and each return exit's source hex/elevation.
        std::vector<MapExit> exitList;
        // Can the player walk back through this edge? Filled by annotateReturnPaths for map-kind
        // edges: unset = undeterminable (destination never analysed, or every return path runs
        // through another elevation — stairs, which this model doesn't trace).
        std::optional<bool> oneWay;
        std::string oneWayReason; ///< "no-return-edge" or "return-unreachable" when oneWay is true
    };

    using Nodes = std::map<std::string, NodeInfo>;
    using Edges = std::map<std::pair<std::string, std::string>, EdgeInfo>;

    // .map filename -> owning worldmap area name, from city.txt entrances (resolved lookup_name ->
    // file). Gives each map_graph node its area, the reverse of world_map's area.maps[].mapFile.
    std::map<std::string, std::string> mapToArea(const CityTxt& city, const resource::MapNameResolver& names) {
        std::map<std::string, std::string> areaOf;
        for (const CityArea& area : city.areas) {
            for (const CityEntrance& entrance : area.entrances) {
                const std::string file = names.fileNameOfLookup(entrance.map);
                if (!file.empty()) {
                    areaOf.emplace(file, area.name); // first area wins if a map is shared
                }
            }
        }
        return areaOf;
    }

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
        edge.exitList.push_back(exit);
    }

    // Load each map and fold its exit grids into the node/edge maps; returns how many maps loaded.
    // `pathOf` records each analysed node's full path so the one-way join can re-load it.
    int buildGraph(resource::GameResources& resources, const resource::MapNameResolver& names,
        const std::vector<std::string>& mapPaths, Nodes& nodes, Edges& edges,
        std::map<std::string, std::string>& pathOf) {
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
            pathOf.emplace(fromFile, mapPath);
            ++analysed;
            for (const MapExit& exit : collectMapExits(*map)) {
                addExit(names, fromFile, exit, nodes, edges);
            }
        }
        return analysed;
    }

    // Per-destination-map reachability cache for the one-way join: the map is re-loaded once (the
    // first pass frees each map after reading its exits) and each elevation's walkable components
    // are computed on first use.
    struct DestReachability {
        std::unique_ptr<Map> map;
        std::map<int, reachability::ElevationResult> byElevation;
        bool loadFailed = false;
    };

    const reachability::ElevationResult& elevationReachability(resource::GameResources& resources,
        DestReachability& dest, int elevation) {
        const auto [it, inserted] = dest.byElevation.try_emplace(elevation);
        if (inserted) {
            static const std::vector<std::shared_ptr<MapObject>> kNoObjects;
            const Map::MapFile& file = dest.map->getMapFile();
            const auto objects = file.map_objects.find(elevation);
            it->second = reachability::analyzeElevation(resources,
                static_cast<int>(file.header.player_default_elevation),
                static_cast<int>(file.header.player_default_position), elevation,
                objects != file.map_objects.end() ? objects->second : kNoObjects);
        }
        return it->second;
    }

    // The walkability half of the one-way join: does some return exit share a walkable component
    // with an arrival hex, on the arrival's elevation? Uses the same optimistic model as the
    // reachability tool (doors passable), so a "return-unreachable" verdict is a real seal, not a
    // locked door.
    void evaluateReturnWalkability(resource::GameResources& resources, EdgeInfo& edge,
        const EdgeInfo& returnEdge, const std::string& toFile,
        const std::map<std::string, std::string>& pathOf, std::map<std::string, DestReachability>& reach) {
        DestReachability& dest = reach[toFile];
        if (!dest.map && !dest.loadFailed) {
            const auto path = pathOf.find(toFile);
            dest.map = path != pathOf.end() ? loadMap(resources, path->second) : nullptr;
            dest.loadFailed = !dest.map;
        }
        if (dest.loadFailed) {
            return; // analysed in the first pass but unreadable now -> leave undetermined
        }
        bool sawSameElevationPair = false;
        bool sawCrossElevationOption = false;
        for (const MapExit& arrival : edge.exitList) {
            const reachability::ElevationResult& result = elevationReachability(resources, dest, arrival.destElevation);
            const int arrivalComponent = reachability::componentOf(arrival.destHex, result.blocked, result.component);
            for (const MapExit& ret : returnEdge.exitList) {
                if (ret.srcElevation != arrival.destElevation) {
                    // A return via another elevation needs stairs, which this model doesn't
                    // trace — it is a possible way back, so it blocks a one-way verdict below.
                    sawCrossElevationOption = true;
                    continue;
                }
                sawSameElevationPair = true;
                if (arrivalComponent != -1
                    && reachability::componentOf(ret.srcHex, result.blocked, result.component) == arrivalComponent) {
                    edge.oneWay = false;
                    return;
                }
            }
        }
        // "return-unreachable" is claimed only when EVERY (arrival, return) combination was
        // determinable and blocked; a cross-elevation return option leaves the verdict unset,
        // since stairs might still connect it.
        if (sawSameElevationPair && !sawCrossElevationOption) {
            edge.oneWay = true;
            edge.oneWayReason = "return-unreachable";
        }
    }

    // The one-way join: for every map-kind edge A->B, can the player walk back to A? Structurally
    // first (does B have an exit grid targeting A at all), then effectively (is some return exit
    // walk-connected to an arrival hex — evaluateReturnWalkability). Non-map edges (worldmap/
    // townmap/unknown) have no "return" concept and stay unset, as do edges into maps that were
    // never analysed.
    void annotateReturnPaths(resource::GameResources& resources, const Nodes& nodes, Edges& edges,
        const std::map<std::string, std::string>& pathOf) {
        std::map<std::string, DestReachability> reach;
        for (auto& [key, edge] : edges) {
            if (edge.kind != "map") {
                continue;
            }
            const auto toNode = nodes.find(key.second);
            if (toNode == nodes.end() || !toNode->second.analysed) {
                continue; // never loaded the destination -> its exits are unknown
            }
            const auto reciprocal = edges.find({ key.second, key.first });
            if (reciprocal == edges.end()) {
                edge.oneWay = true;
                edge.oneWayReason = "no-return-edge";
                continue;
            }
            evaluateReturnWalkability(resources, edge, reciprocal->second, key.second, pathOf, reach);
        }
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
        auto oneWay = ordered_json::array(); // edges the player cannot walk back through
        for (const auto& [key, edge] : edges) {
            if (edge.oneWay == true) {
                oneWay.push_back({ { "from", key.first }, { "to", key.second }, { "reason", edge.oneWayReason } });
            }
        }
        return { { "maps", analysed }, { "nodes", nodes.size() }, { "edges", edges.size() },
            { "deadEnds", std::move(deadEnds) }, { "noIncoming", std::move(noIncoming) },
            { "oneWayEdges", std::move(oneWay) } };
    }

    ordered_json graphToJson(const Nodes& nodes, const Edges& edges, int analysed,
        const resource::MapNameResolver& names, const std::map<std::string, std::string>& areaOf) {
        auto nodesJson = ordered_json::array();
        for (const auto& [file, info] : nodes) {
            const std::string lookupName = names.lookupNameOf(file); // maps.txt key (world_map's entrance name)
            const auto area = areaOf.find(file);                     // owning city.txt area (the reverse join)
            nodesJson.push_back({ { "file", file }, { "displayName", orNull(info.displayName) },
                { "lookupName", orNull(lookupName) },
                { "area", area != areaOf.end() ? ordered_json(area->second) : ordered_json(nullptr) },
                { "analysed", info.analysed } });
        }
        auto edgesJson = ordered_json::array();
        for (const auto& [key, info] : edges) {
            edgesJson.push_back({ { "from", key.first }, { "to", key.second }, { "kind", info.kind },
                { "destMap", info.destMap }, { "toName", orNull(info.toName) }, { "exits", info.exits },
                { "oneWay", info.oneWay.has_value() ? ordered_json(*info.oneWay) : ordered_json(nullptr) },
                { "oneWayReason", orNull(info.oneWayReason) } });
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

    const CityTxt city = loadConfig<CityTxt>(resources, { "data/city.txt", "city.txt" },
        [](const std::string& text) { return parseCityTxt(text); });
    const std::map<std::string, std::string> areaOf = mapToArea(city, names);
    Nodes nodes;
    Edges edges;
    std::map<std::string, std::string> pathOf;
    const int analysed = buildGraph(resources, names, mapPaths, nodes, edges, pathOf);
    annotateReturnPaths(resources, nodes, edges, pathOf);
    out << graphToJson(nodes, edges, analysed, names, areaOf).dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
