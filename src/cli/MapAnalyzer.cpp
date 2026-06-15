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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
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
        char buffer[11];
        std::snprintf(buffer, sizeof(buffer), "0x%08X", pid);
        return buffer;
    }

    const char* objectTypeName(uint32_t type) {
        switch (type) {
            case 0:
                return "item";
            case 1:
                return "critter";
            case 2:
                return "scenery";
            case 3:
                return "wall";
            case 4:
                return "tile";
            case 5:
                return "misc";
            default:
                return "other";
        }
    }

    // Histogram entries sorted by count (descending), then by key for a stable order.
    template <typename K>
    std::vector<std::pair<K, int>> sortedByCountDesc(const std::map<K, int>& histogram) {
        std::vector<std::pair<K, int>> entries(histogram.begin(), histogram.end());
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        return entries;
    }

} // namespace

int analyzeMaps(resource::GameResources& resources, const AnalyzeOptions& options, std::ostream& out) {
    auto& files = resources.files();
    auto& repo = resources.repository();

    std::vector<std::string> mapPaths;
    if (!options.maps.empty()) {
        mapPaths = options.maps;
    } else {
        // List everything and keep the .map files, case-insensitively — more robust than a glob
        // against the raw VFS keys (whose case and leading "/" depend on the DAT/mount).
        const auto allFiles = files.list("*");
        for (const auto& path : allFiles) {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".map") {
                mapPaths.push_back(path.generic_string());
            }
        }
        std::sort(mapPaths.begin(), mapPaths.end());
        if (mapPaths.empty()) {
            out << "No .map files found among " << allFiles.size() << " mounted files.\n";
            return 1;
        }
    }
    if (mapPaths.empty()) {
        out << "No maps given.\n";
        return 1;
    }

    const Lst* tilesLst = repo.load<Lst>("art/tiles/tiles.lst");
    auto tileName = [&](uint16_t id) -> std::string {
        if (tilesLst != nullptr && id < tilesLst->list().size()) {
            return tilesLst->list()[id];
        }
        return "tile#" + std::to_string(id);
    };

    // proto pid -> "<engine name> (<file>.pro)", cached (basePath/Pro/Msg lookups hit the DAT).
    // The readable name comes from the type's message file (proto.msg etc.) keyed by the proto's
    // message_id — same path the UI uses (SelectionPanel) — not typeToString(), which only names
    // the category ("Scenery").
    std::unordered_map<uint32_t, std::string> protoNameCache;
    auto protoName = [&](uint32_t pid) -> std::string {
        if (const auto it = protoNameCache.find(pid); it != protoNameCache.end()) {
            return it->second;
        }
        std::string engineName;
        std::string file;
        try {
            const std::string proPath = ProHelper::basePath(resources, pid);
            file = baseName(proPath);
            if (Pro* pro = repo.load<Pro>(proPath); pro != nullptr) {
                if (Msg* msg = ProHelper::msgFile(resources, pro->type()); msg != nullptr) {
                    engineName = msg->message(pro->header.message_id).text;
                }
            }
        } catch (...) {
            // fall through to whatever we resolved
        }
        std::string label = engineName.empty() ? file : (engineName + " (" + file + ")");
        if (label.empty()) {
            label = "pid#" + std::to_string(pid);
        }
        protoNameCache[pid] = label;
        return label;
    };

    const std::function<Pro*(uint32_t)> proLoad = [&](uint32_t pid) -> Pro* {
        try {
            return repo.load<Pro>(ProHelper::basePath(resources, pid));
        } catch (...) {
            return nullptr;
        }
    };

    std::map<uint16_t, int> aggFloorCount; // floor tile id -> total tiles across all maps
    std::map<uint16_t, int> aggFloorMaps;  // floor tile id -> number of maps that use it
    std::map<uint32_t, int> aggObjCount;   // proto pid -> total instances
    std::map<uint32_t, int> aggObjMaps;    // proto pid -> number of maps that use it
    int analysed = 0;

    for (const auto& mapPath : mapPaths) {
        const auto bytes = files.readRawBytes(mapPath);
        if (!bytes) {
            out << "skip (unreadable): " << mapPath << "\n";
            continue;
        }
        std::unique_ptr<Map> map;
        try {
            MapReader reader(proLoad);
            map = reader.openFile(mapPath, *bytes);
        } catch (...) {
            map = nullptr;
        }
        if (!map) {
            out << "skip (parse failed): " << mapPath << "\n";
            continue;
        }
        ++analysed;

        std::map<uint16_t, int> floors;
        for (const auto& [elevation, tiles] : map->getMapFile().tiles) {
            for (const auto& tile : tiles) {
                if (tile.getFloor() != static_cast<uint16_t>(Map::EMPTY_TILE)) {
                    floors[tile.getFloor()]++;
                }
            }
        }
        std::map<uint32_t, int> objects;
        for (const auto& [elevation, mapObjects] : map->getMapFile().map_objects) {
            for (const auto& object : mapObjects) {
                if (object) {
                    objects[object->pro_pid]++;
                }
            }
        }

        out << "=== " << baseName(mapPath) << " ===\n";
        out << "  floor tiles (" << floors.size() << "):\n";
        for (const auto& [id, count] : sortedByCountDesc(floors)) {
            out << "    " << tileName(id) << "  x" << count << "\n";
            aggFloorCount[id] += count;
            aggFloorMaps[id]++;
        }
        out << "  objects (" << objects.size() << " protos):\n";
        for (const auto& [pid, count] : sortedByCountDesc(objects)) {
            out << "    [" << objectTypeName((pid & 0xFF000000u) >> 24) << "] " << protoName(pid) << "  x" << count << "\n";
            aggObjCount[pid] += count;
            aggObjMaps[pid]++;
        }
        out << "\n";
    }

    out << "==================== AGGREGATE (" << analysed << " maps) ====================\n";
    out << "Floor tiles by total usage:\n";
    for (const auto& [id, count] : sortedByCountDesc(aggFloorCount)) {
        out << "  " << tileName(id) << "  total " << count << ", in " << aggFloorMaps[id] << " maps\n";
    }
    out << "Objects by total usage:\n";
    for (const auto& [pid, count] : sortedByCountDesc(aggObjCount)) {
        out << "  " << pidHex(pid) << " [" << objectTypeName((pid & 0xFF000000u) >> 24) << "] " << protoName(pid)
            << "  total " << count << ", in " << aggObjMaps[pid] << " maps\n";
    }
    return 0;
}

} // namespace geck::cli
