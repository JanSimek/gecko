#include "cli/MapAnalyzer.h"

#include "cli/MapLoad.h"
#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace geck::cli {

namespace {

    std::string baseName(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // The object's raw engine PID, "0x%08X" — the actual identifier a generator/MCP client
    // hands back to placeObject(), so surface it alongside the human-readable name.
    std::string pidHex(uint32_t pid) {
        return std::format("0x{:08X}", pid);
    }

    // Category label for a PID's type byte, via the canonical Pro mapping (no local table).
    std::string typeLabel(uint32_t pid) {
        return Pro::typeToString(Pro::typeOfPid(pid));
    }

    // Histogram entries sorted by count (descending), then by key for a stable order.
    template <typename K>
    std::vector<std::pair<K, int>> sortedByCountDesc(const std::map<K, int>& histogram) {
        std::vector<std::pair<K, int>> entries(histogram.begin(), histogram.end());
        std::ranges::sort(entries, [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        return entries;
    }

    // Per-map usage histograms: floor-tile id -> count, proto pid -> count.
    // An unordered pair of floor-tile ids (a <= b) — a border between two tile types.
    using TilePair = std::pair<uint16_t, uint16_t>;

    struct MapUsage {
        std::map<uint16_t, int> floors;
        std::map<uint32_t, int> objects;
        // Floor-tile borders: how often tile a sits next to a *different* tile b. The data an
        // agent reads to learn a tileset's transitions before generating seamless terrain.
        std::map<TilePair, int> floorAdjacency;
    };

    // Running totals across every analysed map.
    struct Aggregate {
        std::map<uint16_t, int> floorCount; // floor tile id -> total tiles
        std::map<uint16_t, int> floorMaps;  // floor tile id -> number of maps using it
        std::map<uint32_t, int> objCount;   // proto pid -> total instances
        std::map<uint32_t, int> objMaps;    // proto pid -> number of maps using it
        std::map<TilePair, int> adjacency;  // floor-tile border -> total occurrences across maps
        int analysed = 0;
    };

    // Resolves tile and object display names from engine data (cached). The proto name comes
    // from the type's message file keyed by the proto's message_id — the same path the UI uses
    // (SelectionPanel) — not typeToString(), which only names the category ("Scenery").
    class NameResolver {
    public:
        explicit NameResolver(resource::GameResources& resources)
            : _resources(resources)
            , _tilesLst(resources.repository().load<Lst>("art/tiles/tiles.lst")) {
        }

        std::string tileName(uint16_t id) const {
            if (_tilesLst != nullptr && id < _tilesLst->list().size()) {
                return _tilesLst->list()[id];
            }
            return std::format("tile#{}", id);
        }

        std::string protoName(uint32_t pid) {
            if (const auto it = _protoNames.find(pid); it != _protoNames.end()) {
                return it->second;
            }
            std::string label = resolveProtoName(pid);
            _protoNames[pid] = label;
            return label;
        }

        // OBJECT_FLAT marks invisible movement-blockers / floor markers (block.frm). A generator
        // never scatters these; surfacing the flag lets an MCP client tell structural from decor.
        bool isFlat(uint32_t pid) {
            if (const auto it = _flat.find(pid); it != _flat.end()) {
                return it->second;
            }
            bool flat = false;
            try {
                if (const Pro* pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, pid)); pro != nullptr) {
                    flat = Pro::hasFlag(pro->header.flags, Pro::ObjectFlags::OBJECT_FLAT);
                }
            } catch (const std::exception& e) {
                spdlog::debug("isFlat: could not resolve pid {}: {}", pid, e.what());
            }
            _flat[pid] = flat;
            return flat;
        }

    private:
        std::string resolveProtoName(uint32_t pid) {
            std::string engineName;
            std::string file;
            try {
                const std::string proPath = ProHelper::basePath(_resources, pid);
                file = baseName(proPath);
                if (const Pro* pro = _resources.repository().load<Pro>(proPath); pro != nullptr) {
                    if (Msg* msg = ProHelper::msgFile(_resources, pro->type()); msg != nullptr) {
                        engineName = msg->message(pro->header.message_id).text;
                    }
                }
            } catch (const std::exception& e) {
                spdlog::debug("protoName: could not resolve pid {}: {}", pid, e.what());
            }
            if (!engineName.empty()) {
                return std::format("{} ({})", engineName, file);
            }
            return file.empty() ? std::format("pid#{}", pid) : file;
        }

        resource::GameResources& _resources;
        const Lst* _tilesLst;
        std::unordered_map<uint32_t, std::string> _protoNames;
        std::unordered_map<uint32_t, bool> _flat;
    };

    // Minimal JSON string literal (quotes + escapes) — analyze --json builds a fixed schema by
    // hand, so it needs no JSON library.
    std::string jsonString(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('"');
        for (const char c : s) {
            switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                    } else {
                        out.push_back(c);
                    }
            }
        }
        out.push_back('"');
        return out;
    }

    // List everything and keep the .map files, case-insensitively — more robust than a glob
    // against the raw VFS keys (whose case and leading "/" depend on the DAT/mount).
    std::vector<std::string> collectMapPaths(const resource::DataFileSystem& files, const AnalyzeOptions& options, std::ostream& out) {
        if (!options.maps.empty()) {
            return options.maps;
        }
        const auto allFiles = files.list("*");
        std::vector<std::string> mapPaths;
        for (const auto& path : allFiles) {
            std::string ext = path.extension().string();
            std::ranges::transform(ext, ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".map") {
                mapPaths.push_back(path.generic_string());
            }
        }
        std::ranges::sort(mapPaths);
        if (mapPaths.empty()) {
            out << "No .map files found among " << allFiles.size() << " mounted files.\n";
        }
        return mapPaths;
    }

    // Tally floor-tile borders within one elevation's tile grid (row-major, Map::COLS wide). For
    // each non-empty tile only its right and down neighbours are checked, so each adjacent pair is
    // counted once; same-tile borders are ignored — we want the transitions between tile types.
    void collectFloorAdjacency(const std::vector<Tile>& tiles, std::map<TilePair, int>& adjacency) {
        constexpr int width = static_cast<int>(Map::COLS);
        const auto empty = static_cast<uint16_t>(Map::EMPTY_TILE);
        const int count = static_cast<int>(tiles.size());
        for (int i = 0; i < count; ++i) {
            const uint16_t a = tiles[i].getFloor();
            if (a == empty) {
                continue;
            }
            const int col = i % width;
            const std::array<int, 2> neighbours{ col + 1 < width ? i + 1 : -1, i + width < count ? i + width : -1 };
            for (const int j : neighbours) {
                if (j < 0) {
                    continue;
                }
                const uint16_t b = tiles[static_cast<std::size_t>(j)].getFloor();
                if (b != empty && b != a) {
                    adjacency[std::make_pair(std::min(a, b), std::max(a, b))]++;
                }
            }
        }
    }

    MapUsage collectUsage(Map& map) {
        MapUsage usage;
        for (const auto& [elevation, tiles] : map.getMapFile().tiles) {
            for (const auto& tile : tiles) {
                if (tile.getFloor() != static_cast<uint16_t>(Map::EMPTY_TILE)) {
                    usage.floors[tile.getFloor()]++;
                }
            }
            collectFloorAdjacency(tiles, usage.floorAdjacency);
        }
        for (const auto& [elevation, mapObjects] : map.getMapFile().map_objects) {
            for (const auto& object : mapObjects) {
                if (object) {
                    usage.objects[object->pro_pid]++;
                }
            }
        }
        return usage;
    }

    // --- object clustering: group nearby objects so an agent can spot structures (tents, buildings)
    // and feed extract_pattern. Objects within kClusterMergeDistance hexes (Chebyshev) are one
    // cluster; clusters smaller than kMinClusterSize are dropped as decoration noise.
    constexpr int kClusterMergeDistance = 3;
    constexpr int kMinClusterSize = 2;

    struct Cluster {
        int elevation = 0;
        int centerHex = 0; ///< Centroid hex — a good anchorHex for extract_pattern.
        int minHex = 0;    ///< Bounding-box corners as hex indices, to size the capture radius.
        int maxHex = 0;
        int count = 0;
        std::map<uint32_t, int> members; ///< proto pid -> count within the cluster.
    };

    int unionFind(std::vector<int>& parent, int i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    }

    // Union objects that sit within kClusterMergeDistance hexes of each other (single-linkage).
    void linkNearbyObjects(const std::vector<const MapObject*>& objects, std::vector<int>& parent) {
        const int n = static_cast<int>(objects.size());
        for (int i = 0; i < n; ++i) {
            const int ci = hexgrid::columnOf(objects[i]->position);
            const int ri = hexgrid::rowOf(objects[i]->position);
            for (int j = i + 1; j < n; ++j) {
                const int dc = std::abs(ci - hexgrid::columnOf(objects[j]->position));
                const int dr = std::abs(ri - hexgrid::rowOf(objects[j]->position));
                if (std::max(dc, dr) <= kClusterMergeDistance) {
                    parent[unionFind(parent, j)] = unionFind(parent, i);
                }
            }
        }
    }

    struct ClusterAccumulator {
        int count = 0;
        long sumCol = 0;
        long sumRow = 0;
        int minCol = 0;
        int maxCol = 0;
        int minRow = 0;
        int maxRow = 0;
        std::map<uint32_t, int> members;
    };

    void accumulate(ClusterAccumulator& acc, const MapObject& object) {
        const int col = hexgrid::columnOf(object.position);
        const int row = hexgrid::rowOf(object.position);
        if (acc.count == 0) {
            acc.minCol = acc.maxCol = col;
            acc.minRow = acc.maxRow = row;
        }
        acc.minCol = std::min(acc.minCol, col);
        acc.maxCol = std::max(acc.maxCol, col);
        acc.minRow = std::min(acc.minRow, row);
        acc.maxRow = std::max(acc.maxRow, row);
        acc.sumCol += col;
        acc.sumRow += row;
        acc.members[object.pro_pid]++;
        acc.count++;
    }

    Cluster finalizeCluster(const ClusterAccumulator& acc, int elevation) {
        constexpr int hexWidth = HexagonGrid::GRID_WIDTH;
        const int centerCol = static_cast<int>(acc.sumCol / acc.count);
        const int centerRow = static_cast<int>(acc.sumRow / acc.count);
        return Cluster{ elevation, centerRow * hexWidth + centerCol, acc.minRow * hexWidth + acc.minCol,
            acc.maxRow * hexWidth + acc.maxCol, acc.count, acc.members };
    }

    // Cluster the elevation's objects by proximity; keep clusters of at least kMinClusterSize.
    std::vector<Cluster> collectClusters(Map& map) {
        std::vector<Cluster> clusters;
        for (const auto& [elevation, mapObjects] : map.getMapFile().map_objects) {
            std::vector<const MapObject*> objects;
            for (const auto& object : mapObjects) {
                if (object) {
                    objects.push_back(object.get());
                }
            }
            std::vector<int> parent(objects.size());
            std::iota(parent.begin(), parent.end(), 0);
            linkNearbyObjects(objects, parent);

            std::map<int, ClusterAccumulator> byRoot;
            for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
                accumulate(byRoot[unionFind(parent, i)], *objects[i]);
            }
            for (const auto& [root, acc] : byRoot) {
                if (acc.count >= kMinClusterSize) {
                    clusters.push_back(finalizeCluster(acc, elevation));
                }
            }
        }
        std::ranges::sort(clusters, [](const Cluster& a, const Cluster& b) { return a.count > b.count; });
        return clusters;
    }

    // Parse one map, print its per-map palette and fold its usage into `agg`. Returns false
    // (with a "skip" line) if the map can't be read or parsed.
    bool reportMap(const std::string& mapPath, resource::GameResources& resources,
        NameResolver& names, Aggregate& agg, std::ostream& out) {
        const std::unique_ptr<Map> map = loadMap(resources, mapPath);
        if (!map) {
            out << "skip (unreadable or parse failed): " << mapPath << "\n";
            return false;
        }

        const MapUsage usage = collectUsage(*map);
        out << "=== " << baseName(mapPath) << " ===\n";
        out << "  floor tiles (" << usage.floors.size() << "):\n";
        for (const auto& [id, count] : sortedByCountDesc(usage.floors)) {
            out << "    " << names.tileName(id) << "  x" << count << "\n";
            agg.floorCount[id] += count;
            agg.floorMaps[id]++;
        }
        out << "  objects (" << usage.objects.size() << " protos):\n";
        for (const auto& [pid, count] : sortedByCountDesc(usage.objects)) {
            out << "    [" << typeLabel(pid) << "] " << names.protoName(pid) << "  x" << count << "\n";
            agg.objCount[pid] += count;
            agg.objMaps[pid]++;
        }
        out << "\n";
        return true;
    }

    void printAggregate(const Aggregate& agg, NameResolver& names, std::ostream& out) {
        out << "==================== AGGREGATE (" << agg.analysed << " maps) ====================\n";
        out << "Floor tiles by total usage:\n";
        for (const auto& [id, count] : sortedByCountDesc(agg.floorCount)) {
            out << "  " << names.tileName(id) << "  total " << count << ", in " << agg.floorMaps.at(id) << " maps\n";
        }
        out << "Objects by total usage:\n";
        for (const auto& [pid, count] : sortedByCountDesc(agg.objCount)) {
            out << "  " << pidHex(pid) << " [" << typeLabel(pid) << "] " << names.protoName(pid)
                << "  total " << count << ", in " << agg.objMaps.at(pid) << " maps\n";
        }
    }

    // The JSON arrays of emitJson(), one per object below, so emitJson itself stays a flat sketch of
    // the schema. Each prints a comma-separated array body (no brackets) and folds usage into `agg`.

    // Per-map floor array: [{id,name,count}], accumulating totals into agg. tileName() is const
    // (pure tiles.lst lookup), unlike the proto helpers which cache, so names is a const ref here.
    void emitMapFloorsJson(const MapUsage& usage, const NameResolver& names, Aggregate& agg, std::ostream& out) {
        bool first = true;
        for (const auto& [id, count] : sortedByCountDesc(usage.floors)) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"id\":" << id << ",\"name\":" << jsonString(names.tileName(id)) << ",\"count\":" << count << "}";
            agg.floorCount[id] += count;
            agg.floorMaps[id]++;
        }
    }

    // Per-map object array: [{pid,type,name,count,flat}], accumulating totals into agg.
    void emitMapObjectsJson(const MapUsage& usage, NameResolver& names, Aggregate& agg, std::ostream& out) {
        bool first = true;
        for (const auto& [pid, count] : sortedByCountDesc(usage.objects)) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"pid\":" << jsonString(pidHex(pid)) << ",\"type\":" << jsonString(typeLabel(pid))
                << ",\"name\":" << jsonString(names.protoName(pid)) << ",\"count\":" << count
                << ",\"flat\":" << (names.isFlat(pid) ? "true" : "false") << "}";
            agg.objCount[pid] += count;
            agg.objMaps[pid]++;
        }
    }

    // Aggregate floor array: [{id,name,total,maps}]. const names, as in emitMapFloorsJson.
    void emitAggFloorsJson(const Aggregate& agg, const NameResolver& names, std::ostream& out) {
        bool first = true;
        for (const auto& [id, count] : sortedByCountDesc(agg.floorCount)) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"id\":" << id << ",\"name\":" << jsonString(names.tileName(id)) << ",\"total\":" << count
                << ",\"maps\":" << agg.floorMaps.at(id) << "}";
        }
    }

    // Aggregate object array: [{pid,type,name,total,maps,flat}].
    void emitAggObjectsJson(const Aggregate& agg, NameResolver& names, std::ostream& out) {
        bool first = true;
        for (const auto& [pid, count] : sortedByCountDesc(agg.objCount)) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"pid\":" << jsonString(pidHex(pid)) << ",\"type\":" << jsonString(typeLabel(pid))
                << ",\"name\":" << jsonString(names.protoName(pid)) << ",\"total\":" << count
                << ",\"maps\":" << agg.objMaps.at(pid) << ",\"flat\":" << (names.isFlat(pid) ? "true" : "false") << "}";
        }
    }

    // Per-map floor-border array: [{a,aName,b,bName,count}], folding totals into agg.adjacency.
    // const names, as in emitMapFloorsJson (tileName() is the only resolver used).
    void emitMapAdjacencyJson(const MapUsage& usage, const NameResolver& names, Aggregate& agg, std::ostream& out) {
        bool first = true;
        for (const auto& [pair, count] : sortedByCountDesc(usage.floorAdjacency)) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"a\":" << pair.first << ",\"aName\":" << jsonString(names.tileName(pair.first))
                << ",\"b\":" << pair.second << ",\"bName\":" << jsonString(names.tileName(pair.second))
                << ",\"count\":" << count << "}";
            agg.adjacency[pair] += count;
        }
    }

    // Aggregate floor-border array: [{a,aName,b,bName,total}], most-frequent border first.
    void emitAggAdjacencyJson(const Aggregate& agg, const NameResolver& names, std::ostream& out) {
        bool first = true;
        for (const auto& [pair, count] : sortedByCountDesc(agg.adjacency)) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"a\":" << pair.first << ",\"aName\":" << jsonString(names.tileName(pair.first))
                << ",\"b\":" << pair.second << ",\"bName\":" << jsonString(names.tileName(pair.second))
                << ",\"total\":" << count << "}";
        }
    }

    // Per-map cluster array: [{elevation,centerHex,minHex,maxHex,count,members:[{pid,type,name,count}]}].
    void emitMapClustersJson(const std::vector<Cluster>& clusters, NameResolver& names, std::ostream& out) {
        bool first = true;
        for (const auto& cluster : clusters) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"elevation\":" << cluster.elevation << ",\"centerHex\":" << cluster.centerHex
                << ",\"minHex\":" << cluster.minHex << ",\"maxHex\":" << cluster.maxHex
                << ",\"count\":" << cluster.count << ",\"members\":[";
            bool firstMember = true;
            for (const auto& [pid, count] : sortedByCountDesc(cluster.members)) {
                out << (firstMember ? "" : ",");
                firstMember = false;
                out << "{\"pid\":" << jsonString(pidHex(pid)) << ",\"type\":" << jsonString(typeLabel(pid))
                    << ",\"name\":" << jsonString(names.protoName(pid)) << ",\"count\":" << count << "}";
            }
            out << "]}";
        }
    }

    // Machine-readable analyze, for an MCP client: per map { name, path, floor[], objects[],
    // adjacency[], clusters[] } and an aggregate { analysed, floor[], objects[], adjacency[] }.
    // `flat` is the structural-vs-decoration hint; `adjacency` lists floor-tile borders (transitions);
    // `clusters` groups nearby objects (structures like tents/buildings) with a centre/bbox an agent
    // feeds to extract_pattern. A fixed schema emitted by hand (no JSON library).
    void emitJson(const std::vector<std::string>& mapPaths, resource::GameResources& resources,
        NameResolver& names, std::ostream& out) {
        Aggregate agg;
        out << "{\"maps\":[";
        bool firstMap = true;
        for (const auto& mapPath : mapPaths) {
            const std::unique_ptr<Map> map = loadMap(resources, mapPath);
            if (!map) {
                continue; // skipped maps simply don't appear; agg.analysed counts what did
            }
            const MapUsage usage = collectUsage(*map);
            out << (firstMap ? "" : ",");
            firstMap = false;
            out << "{\"name\":" << jsonString(baseName(mapPath)) << ",\"path\":" << jsonString(mapPath)
                << ",\"floor\":[";
            emitMapFloorsJson(usage, names, agg, out);
            out << "],\"objects\":[";
            emitMapObjectsJson(usage, names, agg, out);
            out << "],\"adjacency\":[";
            emitMapAdjacencyJson(usage, names, agg, out);
            out << "],\"clusters\":[";
            emitMapClustersJson(collectClusters(*map), names, out);
            out << "]}";
            ++agg.analysed;
        }
        out << "],\"aggregate\":{\"analysed\":" << agg.analysed << ",\"floor\":[";
        emitAggFloorsJson(agg, names, out);
        out << "],\"objects\":[";
        emitAggObjectsJson(agg, names, out);
        out << "],\"adjacency\":[";
        emitAggAdjacencyJson(agg, names, out);
        out << "]}}\n";
    }

} // namespace

int analyzeMaps(resource::GameResources& resources, const AnalyzeOptions& options, std::ostream& out) {
    const std::vector<std::string> mapPaths = collectMapPaths(resources.files(), options, out);
    if (mapPaths.empty()) {
        return 1;
    }

    NameResolver names(resources);

    if (options.json) {
        emitJson(mapPaths, resources, names, out);
        return 0;
    }

    Aggregate agg;
    for (const auto& mapPath : mapPaths) {
        if (reportMap(mapPath, resources, names, agg, out)) {
            ++agg.analysed;
        }
    }
    printAggregate(agg, names, out);
    return 0;
}

} // namespace geck::cli
