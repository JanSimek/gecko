#include "cli/MapExport.h"

#include "cli/MapAnalyzer.h" // listMapPaths
#include "cli/MapLoad.h"     // loadMap
#include "editor/HexGeometry.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

namespace geck::cli {

namespace {
    using nlohmann::ordered_json;

    // What a consumer needs to show a proto to a reader: its name, the sentence the game prints when
    // you examine it, and the art to draw. Cached, since a proto recurs across hundreds of rows.
    struct ProtoInfo {
        std::string name;
        std::string description;
        std::int32_t fid = 0; // inventory icon for items, the critter's own art otherwise
    };

    // Same name lookup proto_info does — the proto's message_id read out of the type's .msg file —
    // plus the description, which the engine stores at message_id + 1. Kept local rather than reusing
    // MapAnalyzer's NameResolver, which is private to that translation unit.
    class Protos {
    public:
        explicit Protos(resource::GameResources& resources)
            : _resources(resources) {
        }

        const ProtoInfo& operator()(std::uint32_t pid) {
            if (const auto it = _cache.find(pid); it != _cache.end()) {
                return it->second;
            }
            ProtoInfo info;
            try {
                if (const Pro* pro = _resources.loadPro(pid); pro != nullptr) {
                    // Prefer an item's inventory icon, and fall back to its world sprite. Containers
                    // — lockers, footlockers, bookcases, desks — are items by proto type but are never
                    // carried, so their inventoryFID is -1 and only header.FID has art. That is 108 of
                    // the 424 item protos, so without the fallback a quarter of them draw nothing.
                    info.fid = pro->header.FID;
                    if (Pro::typeOfPid(pid) == Pro::OBJECT_TYPE::ITEM
                        && pro->commonItemData.inventoryFID >= 0) {
                        info.fid = pro->commonItemData.inventoryFID;
                    }
                    if (Msg* msg = ProHelper::msgFile(_resources, pro->type()); msg != nullptr) {
                        info.name = msg->message(pro->header.message_id).text;
                        info.description = msg->message(pro->header.message_id + 1).text;
                    }
                }
            } catch (const std::exception& e) {
                spdlog::debug("export: proto {} unreadable: {}", pid, e.what());
            }
            return _cache.try_emplace(pid, std::move(info)).first->second;
        }

        const std::map<std::uint32_t, ProtoInfo>& seen() const { return _cache; }

    private:
        resource::GameResources& _resources;
        std::map<std::uint32_t, ProtoInfo> _cache;
    };

    const char* kindOf(std::uint32_t pid) {
        using enum Pro::OBJECT_TYPE;
        switch (Pro::typeOfPid(pid)) {
            case ITEM:
                return "item";
            case CRITTER:
                return "critter";
            case SCENERY:
                return "scenery";
            case WALL:
                return "wall";
            case TILE:
                return "tile";
            case MISC:
                return "misc";
            default:
                return "unknown";
        }
    }

    bool isSearchable(const MapObject& object, bool includeScenery) {
        if (object.isExitGridMarker()) {
            return true;
        }
        using enum Pro::OBJECT_TYPE;
        const auto type = Pro::typeOfPid(object.pro_pid);
        if (type == ITEM || type == CRITTER) {
            return true;
        }
        return includeScenery && (type == SCENERY || type == WALL);
    }

    // The script attached to an object, as {programIndex, name}, or null. programIndex is the engine's
    // 0-based scripts.lst index (see cli/ScriptIntrospect.h on the index bases).
    ordered_json scriptOf(const Map& map, std::int32_t sid, const Lst* scriptsLst,
        resource::GameResources& resources) {
        if (sid == -1) {
            return nullptr;
        }
        const int section = MapScript::sidSection(static_cast<std::uint32_t>(sid));
        if (section < 0 || section >= Map::SCRIPT_SECTIONS) {
            return nullptr;
        }
        for (const MapScript& script : map.getMapFile().map_scripts[section]) {
            if (script.pid != static_cast<std::uint32_t>(sid)) {
                continue;
            }
            const int programIndex = static_cast<int>(script.script_id);
            std::string name;
            if (scriptsLst != nullptr && programIndex >= 0
                && programIndex < static_cast<int>(scriptsLst->list().size())) {
                name = scriptsLst->at(programIndex);
            }
            ordered_json out;
            out["programIndex"] = programIndex;
            out["name"] = name;
            out["description"] = resource::scriptDescription(resources, programIndex);
            return out;
        }
        return nullptr;
    }

    // Identifies one exit grid by where it leads, so the hexes that make up a single doorway collapse
    // into a single row.
    struct ExitKey {
        std::string map;
        int elevation;
        std::uint32_t destMap;
        std::uint32_t destHex;
        std::uint32_t destElevation;
        auto operator<=>(const ExitKey&) const = default;
    };

    // One row. `hex` is the object's own position, or — for something inside an inventory, which has no
    // position of its own — the position of whatever is holding it.
    ordered_json entityRow(const MapObject& object, const std::string& mapPath, int elevation,
        int hex, Protos& protos, const ordered_json& holder, const ordered_json& script) {
        ordered_json row;
        row["kind"] = object.isExitGridMarker() ? "exitgrid" : kindOf(object.pro_pid);
        row["pid"] = object.pro_pid;
        row["proto"] = object.pro_pid & 0xFFFFFFu;
        row["name"] = protos(object.pro_pid).name;
        row["map"] = mapPath;
        row["elevation"] = elevation;
        row["hex"] = hex;
        row["col"] = hexgrid::columnOf(hex);
        row["row"] = hexgrid::rowOf(hex);
        if (object.amount > 1) {
            row["qty"] = object.amount;
        }
        if (!holder.is_null()) {
            row["holder"] = holder;
        }
        if (!script.is_null()) {
            row["script"] = script;
        }
        if (object.isExitGridMarker()) {
            row["exit"] = { { "map", object.exit_map }, { "hex", object.exit_position },
                { "elevation", object.exit_elevation } };
        }
        return row;
    }
} // namespace

namespace {
    // Collects rows while walking the maps. Holding the walk's state here is what lets
    // exportEntities read as an outline — set up, walk each map, assemble — instead of six levels of
    // nesting around a nested lambda.
    class Collector {
    public:
        Collector(resource::GameResources& resources, const ExportOptions& options, const Lst* scriptsLst)
            : _resources(resources)
            , _options(options)
            , _scriptsLst(scriptsLst)
            , _protos(resources) {
        }

        void walk(Map& map, const std::string& mapPath) {
            for (const auto& [elevation, objects] : map.getMapFile().map_objects) {
                for (const auto& object : objects) {
                    if (object) {
                        addObject(map, mapPath, elevation, *object);
                    }
                }
            }
        }

        Protos& protos() { return _protos; }
        ordered_json takeEntities() { return std::move(_entities); }

    private:
        void addObject(Map& map, const std::string& mapPath, int elevation, const MapObject& object) {
            const int hex = static_cast<int>(object.position);
            if (isSearchable(object, _options.includeScenery)) {
                const ordered_json script = scriptOf(map, object.map_scripts_pid, _scriptsLst, _resources);
                if (_options.groupExits && object.isExitGridMarker()) {
                    addExit(object, mapPath, elevation, hex, script);
                } else {
                    _entities.push_back(
                        entityRow(object, mapPath, elevation, hex, _protos, ordered_json(nullptr), script));
                }
            }
            // Only describe the holder when there is something to hold. Building it unconditionally
            // pulls every scenery proto on every map into the proto table, which is how that table
            // once came out ten times larger than the rows referencing it.
            if (!object.inventory.empty()) {
                addCarried(map, object, describe(object), mapPath, elevation, hex);
            }
        }

        // One row per exit destination rather than per hex: a doorway is a patch of adjacent hexes
        // all leading to the same place, so later hexes only increment the count.
        void addExit(const MapObject& object, const std::string& mapPath, int elevation, int hex,
            const ordered_json& script) {
            const ExitKey key{ mapPath, elevation, object.exit_map, object.exit_position,
                object.exit_elevation };
            if (const auto it = _exitRows.find(key); it != _exitRows.end()) {
                _entities[it->second]["hexes"] = _entities[it->second]["hexes"].get<int>() + 1;
                return;
            }
            ordered_json row
                = entityRow(object, mapPath, elevation, hex, _protos, ordered_json(nullptr), script);
            row["hexes"] = 1;
            _exitRows.try_emplace(key, _entities.size());
            _entities.push_back(std::move(row));
        }

        // Inventory contents are the reason this command exists: a container's items are invisible to
        // analyze/dump_grid, and "where is X" is usually answered by one. An item in an inventory has
        // no position of its own, so it takes its holder's.
        //
        // The recursion is defensive rather than load-bearing. A map stores only one level of
        // inventory — MapReader reads a carried object's own item count but never its items, exactly
        // as the engine does (fallout2-ce objectLoadAllInternal) — so a container inside a container
        // has nothing under it to read. Written to recurse anyway so this stays correct if that ever
        // changes, and so the code is the shape of the data it claims to walk.
        void addCarried(Map& map, const MapObject& holder, const ordered_json& holderRef,
            const std::string& mapPath, int elevation, int hex) {
            for (const auto& carried : holder.inventory) {
                if (!carried) {
                    continue;
                }
                _entities.push_back(entityRow(*carried, mapPath, elevation, hex, _protos, holderRef,
                    scriptOf(map, carried->map_scripts_pid, _scriptsLst, _resources)));
                addCarried(map, *carried, describe(*carried), mapPath, elevation, hex);
            }
        }

        ordered_json describe(const MapObject& object) {
            ordered_json ref;
            ref["kind"] = kindOf(object.pro_pid);
            ref["pid"] = object.pro_pid;
            ref["name"] = _protos(object.pro_pid).name;
            return ref;
        }

        resource::GameResources& _resources;
        const ExportOptions& _options;
        const Lst* _scriptsLst;
        Protos _protos;
        ordered_json _entities = ordered_json::array();
        // Where each already-emitted exit destination landed in `_entities`.
        std::map<ExitKey, std::size_t> _exitRows;
    };

    // The map's own identity: what it is called, and the maps.txt/city.txt keys that join it to the
    // world layer.
    ordered_json mapEntry(const std::string& mapPath, const std::optional<resource::MapNameResolver>& names) {
        const std::string fileName = std::filesystem::path(mapPath).filename().string();
        const int mapIndex = names.has_value() ? names->indexOf(fileName) : -1;
        ordered_json entry;
        entry["file"] = mapPath;
        entry["name"] = fileName;
        const std::string display
            = (names.has_value() && mapIndex >= 0) ? names->displayName(mapIndex, 0) : std::string();
        entry["displayName"] = display.empty() ? ordered_json(nullptr) : ordered_json(display);
        entry["mapIndex"] = mapIndex;
        if (names.has_value() && mapIndex >= 0) {
            const std::string lookup = names->lookupNameOf(fileName);
            entry["lookupName"] = lookup.empty() ? ordered_json(nullptr) : ordered_json(lookup);
        }
        return entry;
    }

    // Every distinct proto the walk touched, so a consumer can show what a thing IS — name, the
    // sentence the game prints on examine, the art to draw — without re-reading the protos itself.
    ordered_json protoTable(Protos& protos) {
        auto table = ordered_json::array();
        for (const auto& [pid, info] : protos.seen()) {
            ordered_json entry;
            entry["pid"] = pid;
            entry["kind"] = kindOf(pid);
            entry["name"] = info.name;
            entry["description"] = info.description;
            entry["fid"] = info.fid;
            table.push_back(std::move(entry));
        }
        return table;
    }
} // namespace

int exportEntities(resource::GameResources& resources, const ExportOptions& options, std::ostream& out) {
    const std::vector<std::string> mapPaths
        = options.maps.empty() ? listMapPaths(resources.files()) : options.maps;
    if (mapPaths.empty()) {
        out << "export: no maps found (is the Fallout 2 data mounted?)\n";
        return 1;
    }

    const Lst* scriptsLst = nullptr;
    try {
        scriptsLst = resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
    } catch (const std::exception& e) {
        spdlog::debug("export: no scripts.lst: {}", e.what());
    }
    std::optional<resource::MapNameResolver> names;
    try {
        names.emplace(resources);
    } catch (const std::exception& e) {
        spdlog::debug("export: no map name resolver: {}", e.what());
    }

    Collector collector(resources, options, scriptsLst);
    auto maps = ordered_json::array();
    auto unreadable = ordered_json::array();

    for (const auto& mapPath : mapPaths) {
        std::string loadError;
        const std::unique_ptr<Map> map = loadMap(resources, mapPath, &loadError);
        if (map == nullptr) {
            unreadable.push_back({ { "map", mapPath }, { "reason", loadError } });
            continue;
        }
        maps.push_back(mapEntry(mapPath, names));
        collector.walk(*map, mapPath);
    }

    ordered_json entities = collector.takeEntities();
    ordered_json root;
    root["maps"] = std::move(maps);
    root["protos"] = protoTable(collector.protos());
    root["mapsUnreadable"] = std::move(unreadable);
    root["entityCount"] = static_cast<int>(entities.size());
    root["entities"] = std::move(entities);
    // Fallout 2 text is CP-1252; `replace` keeps dump() from throwing on stray bytes.
    out << root.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
