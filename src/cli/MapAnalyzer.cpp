#include "cli/MapAnalyzer.h"

#include "cli/MapLoad.h"
#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "format/ai/AiPacket.h"
#include "format/gam/Gam.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "reader/ai/AiTxtReader.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>
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
            characterize(pid);
            return _flat[pid];
        }

        // Whether the proto actually loads from the mounted data. False means the flag/name reported
        // for it are not authoritative (incomplete data) — used to keep unplaceable protos out of the
        // curated palette rather than substituting a guess (engine-data-fidelity rule).
        bool resolved(uint32_t pid) {
            characterize(pid);
            return _resolved[pid];
        }

        // The critter proto's default AI packet (CritterData.aiPacket), cached. 0 if the pid isn't a
        // resolvable critter. Used as the fallback when a map instance leaves ai_packet at 0.
        uint32_t critterAiPacket(uint32_t pid) {
            if (const auto it = _critterAiPackets.find(pid); it != _critterAiPackets.end()) {
                return it->second;
            }
            uint32_t packet = 0;
            try {
                const Pro* pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, pid));
                if (pro != nullptr && pro->type() == Pro::OBJECT_TYPE::CRITTER) {
                    packet = pro->critterData.aiPacket;
                }
            } catch (const std::exception& e) {
                spdlog::debug("critterAiPacket: could not resolve pid {}: {}", pid, e.what());
            }
            _critterAiPackets[pid] = packet;
            return packet;
        }

    private:
        // Load the proto once and cache whether it resolved and its OBJECT_FLAT flag.
        void characterize(uint32_t pid) {
            if (_resolved.find(pid) != _resolved.end()) {
                return;
            }
            bool loaded = false;
            bool flat = false;
            try {
                if (const Pro* pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, pid)); pro != nullptr) {
                    loaded = true;
                    flat = Pro::hasFlag(pro->header.flags, Pro::ObjectFlags::OBJECT_FLAT);
                }
            } catch (const std::exception& e) {
                spdlog::debug("characterize: could not resolve pid {}: {}", pid, e.what());
            }
            _resolved[pid] = loaded;
            _flat[pid] = flat;
        }

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
        std::unordered_map<uint32_t, bool> _resolved;
        std::unordered_map<uint32_t, uint32_t> _critterAiPackets;
    };

    // Load data/ai.txt from the mounted data (empty AiTxt if absent — critters then report just the
    // raw packet number). Tries the canonical path and a bare fallback for odd mounts.
    AiTxt loadAiTxt(resource::GameResources& resources) {
        for (const char* path : { "data/ai.txt", "ai.txt" }) {
            if (const auto bytes = resources.files().readRawBytes(path); bytes.has_value()) {
                return parseAiTxt(std::string(bytes->begin(), bytes->end()));
            }
        }
        return AiTxt{};
    }

    // Ordered so the emitted objects keep the field order below (nlohmann's default json sorts keys).
    using ordered_json = nlohmann::ordered_json;

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

    // Each helper below returns a JSON array/object for emitJson()/emitPalette() to assemble; the
    // per-map ones also fold usage into `agg`. ordered_json keeps the documented field order.

    // Per-map floor array: [{id,name,count}], accumulating totals into agg. tileName() is const
    // (pure tiles.lst lookup), unlike the proto helpers which cache, so names is a const ref here.
    ordered_json floorsToJson(const MapUsage& usage, const NameResolver& names, Aggregate& agg) {
        auto array = ordered_json::array();
        for (const auto& [id, count] : sortedByCountDesc(usage.floors)) {
            array.push_back({ { "id", id }, { "name", names.tileName(id) }, { "count", count } });
            agg.floorCount[id] += count;
            agg.floorMaps[id]++;
        }
        return array;
    }

    // Per-map object array: [{pid,number,type,name,count,flat}], accumulating totals into agg.
    // `number` is the PID's low 24 bits — the value api:proto(type, number) wants (one less than the
    // NNN in the 00000NNN.pro filename), so a script can use it verbatim.
    ordered_json objectsToJson(const MapUsage& usage, NameResolver& names, Aggregate& agg) {
        auto array = ordered_json::array();
        for (const auto& [pid, count] : sortedByCountDesc(usage.objects)) {
            array.push_back({ { "pid", pidHex(pid) }, { "number", pid & 0xFFFFFFu }, { "type", typeLabel(pid) },
                { "name", names.protoName(pid) }, { "count", count }, { "flat", names.isFlat(pid) } });
            agg.objCount[pid] += count;
            agg.objMaps[pid]++;
        }
        return array;
    }

    // Aggregate floor array: [{id,name,total,maps}].
    ordered_json aggFloorsToJson(const Aggregate& agg, const NameResolver& names) {
        auto array = ordered_json::array();
        for (const auto& [id, count] : sortedByCountDesc(agg.floorCount)) {
            array.push_back({ { "id", id }, { "name", names.tileName(id) }, { "total", count }, { "maps", agg.floorMaps.at(id) } });
        }
        return array;
    }

    // Aggregate object array: [{pid,number,type,name,total,maps,flat}].
    ordered_json aggObjectsToJson(const Aggregate& agg, NameResolver& names) {
        auto array = ordered_json::array();
        for (const auto& [pid, count] : sortedByCountDesc(agg.objCount)) {
            array.push_back({ { "pid", pidHex(pid) }, { "number", pid & 0xFFFFFFu }, { "type", typeLabel(pid) },
                { "name", names.protoName(pid) }, { "total", count }, { "maps", agg.objMaps.at(pid) }, { "flat", names.isFlat(pid) } });
        }
        return array;
    }

    // --- palette: the small weighted generation input (floor + scatter scenery) -----------------
    ordered_json paletteFloorToJson(const std::map<uint16_t, int>& floor, const NameResolver& names) {
        auto array = ordered_json::array();
        for (const auto& [id, weight] : sortedByCountDesc(floor)) {
            array.push_back({ { "id", id }, { "name", names.tileName(id) }, { "weight", weight } });
        }
        return array;
    }

    ordered_json paletteSceneryToJson(const std::map<uint32_t, int>& scenery, NameResolver& names) {
        auto array = ordered_json::array();
        for (const auto& [pid, weight] : sortedByCountDesc(scenery)) {
            array.push_back({ { "pid", pidHex(pid) }, { "number", pid & 0xFFFFFFu },
                { "name", names.protoName(pid) }, { "weight", weight } });
        }
        return array;
    }

    // The weighted generation palette aggregated across maps: floor tiles, and the scatter-eligible
    // scenery (scenery type, non-flat — no walls, blockers or flat markers), each with its total
    // placement count as a weight. The small input a generator script needs (what random_desert.luau
    // hardcodes), not the full analyze report. `id`/`number` are ready for api:paintFloor/api:proto.
    void emitPalette(const std::vector<std::string>& mapPaths, resource::GameResources& resources,
        NameResolver& names, std::ostream& out) {
        std::map<uint16_t, int> floor;
        std::map<uint32_t, int> scenery;
        for (const auto& mapPath : mapPaths) {
            const std::unique_ptr<Map> map = loadMap(resources, mapPath);
            if (!map) {
                continue;
            }
            const MapUsage usage = collectUsage(*map);
            for (const auto& [id, count] : usage.floors) {
                floor[id] += count;
            }
            for (const auto& [pid, count] : usage.objects) {
                // Curated palette = scatter-eligible scenery the agent can actually place: scenery
                // type, the proto resolves (no guessing — keep unplaceable protos out), not flat.
                if (Pro::typeOfPid(pid) == Pro::OBJECT_TYPE::SCENERY && names.resolved(pid) && !names.isFlat(pid)) {
                    scenery[pid] += count;
                }
            }
        }
        ordered_json root;
        root["floor"] = paletteFloorToJson(floor, names);
        root["scenery"] = paletteSceneryToJson(scenery, names);
        out << root.dump() << "\n";
    }

    // Per-map floor-border array: [{a,aName,b,bName,count}], folding totals into agg.adjacency.
    ordered_json adjacencyToJson(const MapUsage& usage, const NameResolver& names, Aggregate& agg) {
        auto array = ordered_json::array();
        for (const auto& [pair, count] : sortedByCountDesc(usage.floorAdjacency)) {
            array.push_back({ { "a", pair.first }, { "aName", names.tileName(pair.first) },
                { "b", pair.second }, { "bName", names.tileName(pair.second) }, { "count", count } });
            agg.adjacency[pair] += count;
        }
        return array;
    }

    // Aggregate floor-border array: [{a,aName,b,bName,total}], most-frequent border first.
    ordered_json aggAdjacencyToJson(const Aggregate& agg, const NameResolver& names) {
        auto array = ordered_json::array();
        for (const auto& [pair, count] : sortedByCountDesc(agg.adjacency)) {
            array.push_back({ { "a", pair.first }, { "aName", names.tileName(pair.first) },
                { "b", pair.second }, { "bName", names.tileName(pair.second) }, { "total", count } });
        }
        return array;
    }

    // Per-map cluster array: [{elevation,centerHex,minHex,maxHex,count,members:[{pid,type,name,count}]}].
    ordered_json clustersToJson(const std::vector<Cluster>& clusters, NameResolver& names) {
        auto array = ordered_json::array();
        for (const auto& cluster : clusters) {
            auto members = ordered_json::array();
            for (const auto& [pid, count] : sortedByCountDesc(cluster.members)) {
                members.push_back({ { "pid", pidHex(pid) }, { "type", typeLabel(pid) },
                    { "name", names.protoName(pid) }, { "count", count } });
            }
            array.push_back({ { "elevation", cluster.elevation }, { "centerHex", cluster.centerHex },
                { "minHex", cluster.minHex }, { "maxHex", cluster.maxHex }, { "count", cluster.count },
                { "members", std::move(members) } });
        }
        return array;
    }

    // The resolved AI behaviour sub-object for a packet, or JSON null when ai.txt defines none.
    // Engine labels are kept verbatim (no remapping).
    ordered_json critterAiToJson(const AiPacket* packet) {
        if (packet == nullptr) {
            return nullptr;
        }
        return { { "name", packet->name }, { "aggression", packet->aggression },
            { "disposition", packet->disposition }, { "runAwayMode", packet->runAwayMode },
            { "bestWeapon", packet->bestWeapon }, { "distance", packet->distance },
            { "areaAttackMode", packet->areaAttackMode }, { "secondaryFreq", packet->secondaryFreq } };
    }

    // Per-map critter array: [{pid,number,name,hex,elevation,team,aiPacket,ai:{...}|null}]. `team` is
    // the instance group_id; `aiPacket` is the instance ai_packet, falling back to the proto default
    // when it is 0, resolved through ai.txt into the behaviour sub-object. Lets an agent read who is
    // on the map, which side they fight for, and how they behave.
    ordered_json crittersToJson(Map& map, NameResolver& names, const AiTxt& ai) {
        auto array = ordered_json::array();
        for (const auto& [elevation, mapObjects] : map.getMapFile().map_objects) {
            for (const auto& object : mapObjects) {
                if (!object || object->objectType() != static_cast<uint32_t>(Pro::OBJECT_TYPE::CRITTER)) {
                    continue;
                }
                const uint32_t pid = object->pro_pid;
                const uint32_t packet = object->ai_packet != 0 ? object->ai_packet : names.critterAiPacket(pid);
                array.push_back({ { "pid", pidHex(pid) }, { "number", pid & 0xFFFFFFu },
                    { "name", names.protoName(pid) }, { "hex", object->position }, { "elevation", elevation },
                    { "team", object->group_id }, { "aiPacket", packet },
                    { "ai", critterAiToJson(ai.byPacketNum(static_cast<int>(packet))) } });
            }
        }
        return array;
    }

    // The map's companion .gam (same basename), which names its map variables, or nullptr if absent.
    const Gam* loadMapGam(resource::GameResources& resources, const std::string& mapPath) {
        const auto dot = mapPath.find_last_of('.');
        const std::string gamPath = (dot == std::string::npos ? mapPath : mapPath.substr(0, dot)) + ".gam";
        try {
            return resources.repository().load<Gam>(gamPath);
        } catch (const std::exception& e) {
            spdlog::debug("loadMapGam: no .gam for {}: {}", mapPath, e.what());
            return nullptr;
        }
    }

    // Per-map header digest: player spawn, which elevations are enabled, darkness, the map script id,
    // the local-var pool size, and the map variables (MVARS) — each with its name from the .gam (the
    // .map stores only the value) so an agent reads the map's tracked state, not just a count.
    ordered_json headerToJson(Map& map, const Gam* gam) {
        const auto& header = map.getMapFile().header;
        ordered_json root;
        root["version"] = header.version;
        root["player"] = { { "position", header.player_default_position },
            { "elevation", header.player_default_elevation }, { "orientation", header.player_default_orientation } };
        auto elevations = ordered_json::array();
        for (int elevation = 0; elevation < Map::ELEVATION_COUNT; ++elevation) {
            if (Map::elevationIsPresent(header.flags, elevation)) {
                elevations.push_back(elevation);
            }
        }
        root["elevations"] = std::move(elevations);
        root["darkness"] = header.darkness;
        root["scriptId"] = header.script_id;
        root["localVarCount"] = header.num_local_vars;
        auto mapVarsJson = ordered_json::array();
        const auto& mapVars = map.getMapFile().map_global_vars;
        for (std::size_t i = 0; i < mapVars.size(); ++i) {
            std::string name = "MVAR#" + std::to_string(i);
            if (gam != nullptr) {
                try {
                    name = gam->mvarKey(i);
                } catch (const std::exception&) { // .gam shorter than the value block — fall back to the index
                }
            }
            mapVarsJson.push_back({ { "index", i }, { "name", name }, { "value", mapVars[i] } });
        }
        root["mapVars"] = std::move(mapVarsJson);
        return root;
    }

    // Per-map exit graph: [{hex,elevation,destMap,destHex,destElevation,orientation}] from the
    // exit-grid markers. destMap/destHex are signed (-1 = town map / unused, -2 = worldmap, else a
    // map id) — the map's connectivity, so an agent sees where each edge leads.
    ordered_json exitsToJson(Map& map) {
        auto array = ordered_json::array();
        for (const auto& [elevation, mapObjects] : map.getMapFile().map_objects) {
            for (const auto& object : mapObjects) {
                if (!object || !object->isExitGridMarker()) {
                    continue;
                }
                array.push_back({ { "hex", object->position }, { "elevation", elevation },
                    { "destMap", static_cast<int32_t>(object->exit_map) },
                    { "destHex", static_cast<int32_t>(object->exit_position) },
                    { "destElevation", object->exit_elevation }, { "orientation", object->exit_orientation } });
            }
        }
        return array;
    }

    // One map's full report object, folding its floor/object/adjacency usage into `agg` as a side
    // effect (so the aggregate built afterwards has every map's totals).
    ordered_json mapToJson(const std::string& mapPath, Map& map, resource::GameResources& resources,
        NameResolver& names, const AiTxt& ai, Aggregate& agg) {
        const MapUsage usage = collectUsage(map);
        ordered_json entry;
        entry["name"] = baseName(mapPath);
        entry["path"] = mapPath;
        entry["floor"] = floorsToJson(usage, names, agg);
        entry["objects"] = objectsToJson(usage, names, agg);
        entry["adjacency"] = adjacencyToJson(usage, names, agg);
        entry["clusters"] = clustersToJson(collectClusters(map), names);
        entry["critters"] = crittersToJson(map, names, ai);
        entry["header"] = headerToJson(map, loadMapGam(resources, mapPath));
        entry["exits"] = exitsToJson(map);
        return entry;
    }

    // Machine-readable analyze, for an MCP client: per map { name, path, floor[], objects[],
    // adjacency[], clusters[], critters[], header{}, exits[] } and an aggregate { analysed, floor[],
    // objects[], adjacency[] }. `flat` is the structural-vs-decoration hint; `adjacency` lists
    // floor-tile borders (transitions); `clusters` groups nearby objects (structures) with a
    // centre/bbox an agent feeds to extract_pattern. Built with nlohmann ordered_json.
    void emitJson(const std::vector<std::string>& mapPaths, resource::GameResources& resources,
        NameResolver& names, const AiTxt& ai, std::ostream& out) {
        Aggregate agg;
        auto maps = ordered_json::array();
        for (const auto& mapPath : mapPaths) {
            const std::unique_ptr<Map> map = loadMap(resources, mapPath);
            if (!map) {
                continue; // skipped maps simply don't appear; agg.analysed counts what did
            }
            maps.push_back(mapToJson(mapPath, *map, resources, names, ai, agg));
            ++agg.analysed;
        }

        ordered_json root;
        root["maps"] = std::move(maps);
        ordered_json aggregate;
        aggregate["analysed"] = agg.analysed;
        aggregate["floor"] = aggFloorsToJson(agg, names);
        aggregate["objects"] = aggObjectsToJson(agg, names);
        aggregate["adjacency"] = aggAdjacencyToJson(agg, names);
        root["aggregate"] = std::move(aggregate);
        out << root.dump() << "\n";
    }

} // namespace

int analyzeMaps(resource::GameResources& resources, const AnalyzeOptions& options, std::ostream& out) {
    const std::vector<std::string> mapPaths = collectMapPaths(resources.files(), options, out);
    if (mapPaths.empty()) {
        return 1;
    }

    NameResolver names(resources);

    if (options.palette) {
        emitPalette(mapPaths, resources, names, out);
        return 0;
    }
    if (options.json) {
        emitJson(mapPaths, resources, names, loadAiTxt(resources), out);
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
