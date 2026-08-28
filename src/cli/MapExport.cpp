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
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

namespace geck::cli {

namespace {
    using nlohmann::ordered_json;

    // Proto display name, cached. Same lookup proto_info does: the proto's message_id read out of the
    // type's .msg file. Kept local rather than reusing MapAnalyzer's NameResolver, which is private to
    // that translation unit.
    class ProtoNames {
    public:
        explicit ProtoNames(resource::GameResources& resources)
            : _resources(resources) {
        }

        std::string operator()(std::uint32_t pid) {
            if (const auto it = _cache.find(pid); it != _cache.end()) {
                return it->second;
            }
            std::string name;
            try {
                if (const Pro* pro = _resources.loadPro(pid); pro != nullptr) {
                    if (Msg* msg = ProHelper::msgFile(_resources, pro->type()); msg != nullptr) {
                        name = msg->message(pro->header.message_id).text;
                    }
                }
            } catch (const std::exception& e) {
                spdlog::debug("export: proto {} has no name: {}", pid, e.what());
            }
            _cache.emplace(pid, name);
            return name;
        }

    private:
        resource::GameResources& _resources;
        std::unordered_map<std::uint32_t, std::string> _cache;
    };

    const char* kindOf(std::uint32_t pid) {
        switch (Pro::typeOfPid(pid)) {
            case Pro::OBJECT_TYPE::ITEM:
                return "item";
            case Pro::OBJECT_TYPE::CRITTER:
                return "critter";
            case Pro::OBJECT_TYPE::SCENERY:
                return "scenery";
            case Pro::OBJECT_TYPE::WALL:
                return "wall";
            case Pro::OBJECT_TYPE::TILE:
                return "tile";
            case Pro::OBJECT_TYPE::MISC:
                return "misc";
            default:
                return "unknown";
        }
    }

    bool isSearchable(const MapObject& object, bool includeScenery) {
        if (object.isExitGridMarker()) {
            return true;
        }
        const auto type = Pro::typeOfPid(object.pro_pid);
        if (type == Pro::OBJECT_TYPE::ITEM || type == Pro::OBJECT_TYPE::CRITTER) {
            return true;
        }
        return includeScenery
            && (type == Pro::OBJECT_TYPE::SCENERY || type == Pro::OBJECT_TYPE::WALL);
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
        int hex, ProtoNames& protoName, const ordered_json& holder, const ordered_json& script) {
        ordered_json row;
        row["kind"] = object.isExitGridMarker() ? "exitgrid" : kindOf(object.pro_pid);
        row["pid"] = object.pro_pid;
        row["proto"] = object.pro_pid & 0xFFFFFFu;
        row["name"] = protoName(object.pro_pid);
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

int exportEntities(resource::GameResources& resources, const ExportOptions& options, std::ostream& out) {
    const std::vector<std::string> mapPaths = options.maps.empty()
        ? listMapPaths(resources.files())
        : options.maps;
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

    ProtoNames protoName(resources);
    // Where each already-emitted exit destination landed in `entities`, so its hexes can be counted
    // into the existing row instead of adding another.
    std::map<ExitKey, std::size_t> exitRows;
    auto maps = ordered_json::array();
    auto entities = ordered_json::array();
    auto unreadable = ordered_json::array();

    for (const auto& mapPath : mapPaths) {
        std::string loadError;
        const std::unique_ptr<Map> map = loadMap(resources, mapPath, &loadError);
        if (map == nullptr) {
            unreadable.push_back({ { "map", mapPath }, { "reason", loadError } });
            continue;
        }

        const std::string fileName = std::filesystem::path(mapPath).filename().string();
        const int mapIndex = names.has_value() ? names->indexOf(fileName) : -1;
        std::string display;
        if (names.has_value() && mapIndex >= 0) {
            display = names->displayName(mapIndex, 0);
        }
        ordered_json mapEntry;
        mapEntry["file"] = mapPath;
        mapEntry["name"] = fileName;
        mapEntry["displayName"] = display.empty() ? ordered_json(nullptr) : ordered_json(display);
        mapEntry["mapIndex"] = mapIndex;
        if (names.has_value() && mapIndex >= 0) {
            const std::string lookup = names->lookupNameOf(fileName);
            mapEntry["lookupName"] = lookup.empty() ? ordered_json(nullptr) : ordered_json(lookup);
        }
        maps.push_back(std::move(mapEntry));

        for (const auto& [elevation, objects] : map->getMapFile().map_objects) {
            for (const auto& object : objects) {
                if (!object) {
                    continue;
                }
                const int hex = static_cast<int>(object->position);
                const ordered_json script = scriptOf(*map, object->map_scripts_pid, scriptsLst, resources);
                if (isSearchable(*object, options.includeScenery)) {
                    if (options.groupExits && object->isExitGridMarker()) {
                        // One row per destination rather than per hex; count the hexes instead.
                        const ExitKey key{ mapPath, elevation, object->exit_map,
                            object->exit_position, object->exit_elevation };
                        if (const auto it = exitRows.find(key); it != exitRows.end()) {
                            entities[it->second]["hexes"] = entities[it->second]["hexes"].get<int>() + 1;
                        } else {
                            ordered_json row = entityRow(*object, mapPath, elevation, hex, protoName,
                                ordered_json(nullptr), script);
                            row["hexes"] = 1;
                            exitRows.emplace(key, entities.size());
                            entities.push_back(std::move(row));
                        }
                    } else {
                        entities.push_back(entityRow(*object, mapPath, elevation, hex, protoName,
                            ordered_json(nullptr), script));
                    }
                }
                // Inventory contents are the reason this command exists: a container's items are
                // invisible to analyze/dump_grid, and "where is X" is usually answered by one.
                for (const auto& carried : object->inventory) {
                    if (!carried) {
                        continue;
                    }
                    ordered_json holder;
                    holder["kind"] = kindOf(object->pro_pid);
                    holder["pid"] = object->pro_pid;
                    holder["name"] = protoName(object->pro_pid);
                    entities.push_back(entityRow(*carried, mapPath, elevation, hex, protoName,
                        holder, ordered_json(nullptr)));
                }
            }
        }
    }

    ordered_json root;
    root["maps"] = std::move(maps);
    root["mapsUnreadable"] = std::move(unreadable);
    root["entityCount"] = static_cast<int>(entities.size());
    root["entities"] = std::move(entities);
    // Fallout 2 text is CP-1252; `replace` keeps dump() from throwing on stray bytes.
    out << root.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
