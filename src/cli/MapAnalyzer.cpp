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

    // Read + parse a map headlessly; nullptr if it can't be read/parsed. Shared by the text and
    // JSON paths.
    std::unique_ptr<Map> loadMapForAnalysis(const std::string& mapPath, resource::GameResources& resources,
        const std::function<Pro*(uint32_t)>& proLoad) {
        const auto bytes = resources.files().readRawBytes(mapPath);
        if (!bytes) {
            return nullptr;
        }
        try {
            MapReader reader(proLoad);
            return reader.openFile(mapPath, *bytes);
        } catch (const std::exception& e) {
            spdlog::debug("loadMapForAnalysis: parse failed for {}: {}", mapPath, e.what());
            return nullptr;
        }
    }

    // Parse one map, print its per-map palette and fold its usage into `agg`. Returns false
    // (with a "skip" line) if the map can't be read or parsed.
    bool reportMap(const std::string& mapPath, resource::GameResources& resources,
        const std::function<Pro*(uint32_t)>& proLoad, NameResolver& names, Aggregate& agg, std::ostream& out) {
        const std::unique_ptr<Map> map = loadMapForAnalysis(mapPath, resources, proLoad);
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

    // Machine-readable analyze, for an MCP client: { maps: [{name, path, floor:[{id,name,count}],
    // objects:[{pid,type,name,count,flat}]}], aggregate: {analysed, floor:[...], objects:[...]} }.
    // A fixed schema emitted by hand (no JSON library); `flat` is the structural-vs-decoration hint.
    void emitJson(const std::vector<std::string>& mapPaths, resource::GameResources& resources,
        const std::function<Pro*(uint32_t)>& proLoad, NameResolver& names, std::ostream& out) {
        Aggregate agg;
        out << "{\"maps\":[";
        bool firstMap = true;
        for (const auto& mapPath : mapPaths) {
            const std::unique_ptr<Map> map = loadMapForAnalysis(mapPath, resources, proLoad);
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
            out << "]}";
            ++agg.analysed;
        }
        out << "],\"aggregate\":{\"analysed\":" << agg.analysed << ",\"floor\":[";
        emitAggFloorsJson(agg, names, out);
        out << "],\"objects\":[";
        emitAggObjectsJson(agg, names, out);
        out << "]}}\n";
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

    if (options.json) {
        emitJson(mapPaths, resources, proLoad, names, out);
        return 0;
    }

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
