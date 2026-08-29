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
            return _cache.emplace(pid, std::move(info)).first->second;
        }

        const std::map<std::uint32_t, ProtoInfo>& seen() const { return _cache; }

    private:
        resource::GameResources& _resources;
        std::map<std::uint32_t, ProtoInfo> _cache;
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

    Protos protos(resources);
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
                            ordered_json row = entityRow(*object, mapPath, elevation, hex, protos,
                                ordered_json(nullptr), script);
                            row["hexes"] = 1;
                            exitRows.emplace(key, entities.size());
                            entities.push_back(std::move(row));
                        }
                    } else {
                        entities.push_back(entityRow(*object, mapPath, elevation, hex, protos,
                            ordered_json(nullptr), script));
                    }
                }
                // Inventory contents are the reason this command exists: a container's items are
                // invisible to analyze/dump_grid, and "where is X" is usually answered by one.
                //
                // The recursion is defensive rather than load-bearing. A map stores only one level of
                // inventory — MapReader reads a carried object's own item count but never its items,
                // exactly as the engine does (fallout2-ce objectLoadAllInternal), so a container
                // inside a container has nothing under it to read. Written to recurse anyway so this
                // stays correct if that ever changes, and so the shape of the code matches the shape
                // of the data it claims to walk.
                const std::function<void(const MapObject&, const ordered_json&)> emitCarried
                    = [&](const MapObject& holderObject, const ordered_json& holderRef) {
                          for (const auto& carried : holderObject.inventory) {
                              if (!carried) {
                                  continue;
                              }
                              entities.push_back(entityRow(*carried, mapPath, elevation, hex, protos,
                                  holderRef,
                                  scriptOf(*map, carried->map_scripts_pid, scriptsLst, resources)));
                              ordered_json nested;
                              nested["kind"] = kindOf(carried->pro_pid);
                              nested["pid"] = carried->pro_pid;
                              nested["name"] = protos(carried->pro_pid).name;
                              emitCarried(*carried, nested);
                          }
                      };
                // Only describe the holder when there is something to hold. Building it
                // unconditionally would pull every scenery proto on every map into the proto table,
                // which is how it ended up ten times larger than the rows that reference it.
                if (!object->inventory.empty()) {
                    ordered_json holder;
                    holder["kind"] = kindOf(object->pro_pid);
                    holder["pid"] = object->pro_pid;
                    holder["name"] = protos(object->pro_pid).name;
                    emitCarried(*object, holder);
                }
            }
        }
    }

    // Every distinct proto the walk touched, so a consumer can show what a thing IS — name, the
    // sentence the game prints on examine, and the art id to draw — without re-reading the protos
    // itself. A few hundred entries against tens of thousands of rows, so it is cheap to carry here
    // and expensive to look up any other way.
    auto protoTable = ordered_json::array();
    for (const auto& [pid, info] : protos.seen()) {
        ordered_json entry;
        entry["pid"] = pid;
        entry["kind"] = kindOf(pid);
        entry["name"] = info.name;
        entry["description"] = info.description;
        entry["fid"] = info.fid;
        protoTable.push_back(std::move(entry));
    }

    ordered_json root;
    root["maps"] = std::move(maps);
    root["protos"] = std::move(protoTable);
    root["mapsUnreadable"] = std::move(unreadable);
    root["entityCount"] = static_cast<int>(entities.size());
    root["entities"] = std::move(entities);
    // Fallout 2 text is CP-1252; `replace` keeps dump() from throwing on stray bytes.
    out << root.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
