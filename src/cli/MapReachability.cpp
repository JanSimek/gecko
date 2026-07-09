#include "cli/MapReachability.h"

#include "cli/MapLoad.h"
#include "editor/HexGeometry.h"
#include "editor/Reachability.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace geck::cli {

namespace {
    using nlohmann::ordered_json;

    constexpr int kHexCount = hexgrid::WIDTH * hexgrid::HEIGHT;
    constexpr std::size_t kMaxReported = 25; // cap the orphan list; we also report the true total

    // Filename without directory or extension ("maps/vctydwtn.map" -> "vctydwtn").
    std::string baseName(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
        const auto dot = file.find_last_of('.');
        return dot == std::string::npos ? file : file.substr(0, dot);
    }

    std::string protoName(resource::GameResources& resources, uint32_t pid) {
        try {
            if (const Pro* pro = resources.loadPro(pid); pro != nullptr) {
                if (Msg* msg = ProHelper::msgFile(resources, pro->type()); msg != nullptr) {
                    return msg->message(pro->header.message_id).text;
                }
            }
        } catch (const std::exception&) { // a missing/odd proto just yields an empty name
        }
        return {};
    }

    // Only critters and items are gameplay content worth flagging as "orphaned" if cut off.
    bool isGameplayObject(const MapObject& object) {
        const auto type = object.objectType();
        return type == static_cast<uint32_t>(Pro::OBJECT_TYPE::CRITTER)
            || type == static_cast<uint32_t>(Pro::OBJECT_TYPE::ITEM);
    }

    // Per exit grid, whether the player can walk to it *from the start specifically* (same component as
    // the player start). null when the player spawns on another elevation.
    ordered_json exitsJson(const std::vector<std::shared_ptr<MapObject>>& objects, const std::vector<char>& blocked,
        const std::vector<int>& component, const std::vector<char>& playerStartFlags, bool playerHere) {
        auto array = ordered_json::array();
        for (const auto& object : objects) {
            if (!object || !object->isExitGridMarker() || object->position < 0 || object->position >= kHexCount) {
                continue;
            }
            ordered_json exit;
            exit["hex"] = object->position;
            if (!playerHere) {
                exit["reachableFromPlayerStart"] = nullptr;
            } else {
                const int comp = reachability::componentOf(object->position, blocked, component);
                exit["reachableFromPlayerStart"] = comp >= 0 && playerStartFlags[comp] != 0;
            }
            array.push_back(exit);
        }
        return array;
    }

    // Critters/items cut off from every entry point.
    ordered_json orphanedObjectsJson(resource::GameResources& resources, const std::vector<std::shared_ptr<MapObject>>& objects,
        const std::vector<char>& blocked, const std::vector<int>& component, const std::vector<char>& entryFlags, std::size_t& totalOut) {
        auto array = ordered_json::array();
        std::size_t total = 0;
        for (const auto& object : objects) {
            if (!object || !isGameplayObject(*object) || object->position < 0 || object->position >= kHexCount) {
                continue;
            }
            if (reachability::reaches(object->position, blocked, component, entryFlags)) {
                continue;
            }
            ++total;
            if (array.size() < kMaxReported) {
                array.push_back({ { "pid", object->pro_pid }, { "name", protoName(resources, object->pro_pid) },
                    { "hex", object->position } });
            }
        }
        totalOut = total;
        return array;
    }

    ordered_json elevationToJson(resource::GameResources& resources, const Map::MapHeader& header, int elevation,
        const std::vector<std::shared_ptr<MapObject>>& objects, const std::string& mapName) {
        const reachability::ElevationResult r = reachability::analyzeElevation(resources,
            static_cast<int>(header.player_default_elevation), static_cast<int>(header.player_default_position),
            elevation, objects);

        int walkable = 0;
        for (const int size : r.componentSizes) {
            walkable += size;
        }

        ordered_json entry;
        entry["elevation"] = elevation;
        entry["walkableHexes"] = walkable;
        entry["blockedHexes"] = kHexCount - walkable;

        entry["playerStartHex"] = r.playerHere ? ordered_json(static_cast<int>(header.player_default_position)) : ordered_json(nullptr);

        entry["entryPointHexes"] = static_cast<int>(r.entrySeeds.size());
        if (!r.hasEntryPoints()) {
            // No player start and no exit grid here — entered via stairs/elevators, which the hex flood
            // doesn't model, so reachability can't be determined.
            entry["reachableHexes"] = nullptr;
            entry["exits"] = ordered_json::array();
            entry["orphanedObjects"] = ordered_json::array();
            entry["orphanedObjectCount"] = 0;
            entry["note"] = "no entry point on this elevation (reached via stairs/elevators, not modeled)";
            return entry;
        }

        // Reachability/orphans use every entry point (the player can arrive at the start *or* at an
        // exit grid coming from the adjacent map); the per-exit flag below is start-only.
        int reachable = 0;
        for (std::size_t id = 0; id < r.componentSizes.size(); ++id) {
            if (r.entryReachable[id]) {
                reachable += r.componentSizes[id];
            }
        }
        entry["reachableHexes"] = reachable;

        entry["exits"] = exitsJson(objects, r.blocked, r.component, r.playerStartReachable, r.playerHere);

        std::size_t orphanTotal = 0;
        entry["orphanedObjects"] = orphanedObjectsJson(resources, objects, r.blocked, r.component, r.entryReachable, orphanTotal);
        entry["orphanedObjectCount"] = static_cast<int>(orphanTotal);
        if (orphanTotal > kMaxReported) {
            spdlog::debug("reachability {} elev {}: reporting {} of {} orphaned objects", mapName, elevation, kMaxReported, orphanTotal);
        }
        return entry;
    }
} // namespace

int analyzeReachability(resource::GameResources& resources, const ReachabilityOptions& options, std::ostream& out) {
    const std::unique_ptr<Map> map = loadMap(resources, options.mapPath);
    if (!map) {
        out << "reachability: cannot load map " << options.mapPath << " (is the Fallout 2 data mounted?)\n";
        return 1;
    }
    const auto& mapFile = map->getMapFile();
    const std::string mapName = baseName(options.mapPath);

    ordered_json root;
    root["map"] = mapName;
    root["path"] = options.mapPath;
    auto elevations = ordered_json::array();
    static const std::vector<std::shared_ptr<MapObject>> kNoObjects;
    for (int elevation = 0; elevation < Map::ELEVATION_COUNT; ++elevation) {
        if (!Map::elevationIsPresent(mapFile.header.flags, elevation)) {
            continue;
        }
        const auto it = mapFile.map_objects.find(elevation);
        const auto& objects = it != mapFile.map_objects.end() ? it->second : kNoObjects;
        elevations.push_back(elevationToJson(resources, mapFile.header, elevation, objects, mapName));
    }
    root["reachability"] = std::move(elevations);
    out << root.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
