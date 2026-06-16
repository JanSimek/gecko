#include "cli/MapAnalyzer.h"

#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
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
    struct MapUsage {
        std::map<uint16_t, int> floors;
        std::map<uint32_t, int> objects;
    };

    // Running totals across every analysed map.
    struct Aggregate {
        std::map<uint16_t, int> floorCount; // floor tile id -> total tiles
        std::map<uint16_t, int> floorMaps;  // floor tile id -> number of maps using it
        std::map<uint32_t, int> objCount;   // proto pid -> total instances
        std::map<uint32_t, int> objMaps;    // proto pid -> number of maps using it
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
    };

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

    MapUsage collectUsage(Map& map) {
        MapUsage usage;
        for (const auto& [elevation, tiles] : map.getMapFile().tiles) {
            for (const auto& tile : tiles) {
                if (tile.getFloor() != static_cast<uint16_t>(Map::EMPTY_TILE)) {
                    usage.floors[tile.getFloor()]++;
                }
            }
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

    // Parse one map, print its per-map palette and fold its usage into `agg`. Returns false
    // (with a "skip" line) if the map can't be read or parsed.
    bool reportMap(const std::string& mapPath, resource::GameResources& resources,
        const std::function<Pro*(uint32_t)>& proLoad, NameResolver& names, Aggregate& agg, std::ostream& out) {
        const auto bytes = resources.files().readRawBytes(mapPath);
        if (!bytes) {
            out << "skip (unreadable): " << mapPath << "\n";
            return false;
        }
        std::unique_ptr<Map> map;
        try {
            MapReader reader(proLoad);
            map = reader.openFile(mapPath, *bytes);
        } catch (const std::exception& e) {
            spdlog::debug("reportMap: parse failed for {}: {}", mapPath, e.what());
        }
        if (!map) {
            out << "skip (parse failed): " << mapPath << "\n";
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

} // namespace

int analyzeMaps(resource::GameResources& resources, const AnalyzeOptions& options, std::ostream& out) {
    const std::vector<std::string> mapPaths = collectMapPaths(resources.files(), options, out);
    if (mapPaths.empty()) {
        return 1;
    }

    NameResolver names(resources);
    const std::function<Pro*(uint32_t)> proLoad = [&resources](uint32_t pid) -> Pro* {
        try {
            return resources.repository().load<Pro>(ProHelper::basePath(resources, pid));
        } catch (const std::exception& e) {
            spdlog::debug("proLoad: pid {} failed: {}", pid, e.what());
            return nullptr;
        }
    };

    Aggregate agg;
    for (const auto& mapPath : mapPaths) {
        if (reportMap(mapPath, resources, proLoad, names, agg, out)) {
            ++agg.analysed;
        }
    }
    printAggregate(agg, names, out);
    return 0;
}

} // namespace geck::cli
