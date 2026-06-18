#include "cli/MapReachability.h"

#include "cli/MapLoad.h"
#include "editor/HexGeometry.h"
#include "editor/helper/ObjectQueries.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <ostream>
#include <queue>
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
            if (const Pro* pro = resources.repository().load<Pro>(ProHelper::basePath(resources, pid)); pro != nullptr) {
                if (Msg* msg = ProHelper::msgFile(resources, pro->type()); msg != nullptr) {
                    return msg->message(pro->header.message_id).text;
                }
            }
        } catch (const std::exception&) { // a missing/odd proto just yields an empty name
        }
        return {};
    }

    // A door blocks only until opened; for reachability the player can open it, so doors are passable.
    bool isDoor(resource::GameResources& resources, const MapObject& object) {
        try {
            if (const Pro* pro = resources.repository().load<Pro>(ProHelper::basePath(resources, object.pro_pid)); pro != nullptr) {
                return pro->type() == Pro::OBJECT_TYPE::SCENERY
                    && static_cast<Pro::SCENERY_TYPE>(pro->objectSubtypeId()) == Pro::SCENERY_TYPE::DOOR;
            }
        } catch (const std::exception&) {
        }
        return false;
    }

    // Walls and most scenery are *meant* to sit in/among blocked space; only critters and items being
    // cut off from the playable area is a meaningful "orphan".
    bool isGameplayObject(const MapObject& object) {
        const auto type = object.objectType();
        return type == static_cast<uint32_t>(Pro::OBJECT_TYPE::CRITTER)
            || type == static_cast<uint32_t>(Pro::OBJECT_TYPE::ITEM);
    }

    // Impassable-hex mask for one elevation: an object blocks movement and is not a (openable) door.
    std::vector<char> blockedMask(resource::GameResources& resources, const std::vector<std::shared_ptr<MapObject>>& objects) {
        std::vector<char> blocked(kHexCount, 0);
        for (const auto& object : objects) {
            if (object && object->position >= 0 && object->position < kHexCount
                && object_query::blocksMovement(*object, resources) && !isDoor(resources, *object)) {
                blocked[object->position] = 1;
            }
        }
        return blocked;
    }

    // Entry hexes for an elevation: the player start (if it loads here) plus every exit-grid marker.
    std::vector<int> entryHexes(const Map::MapHeader& header, int elevation, const std::vector<std::shared_ptr<MapObject>>& objects) {
        std::vector<int> seeds;
        if (static_cast<int>(header.player_default_elevation) == elevation) {
            seeds.push_back(static_cast<int>(header.player_default_position));
        }
        for (const auto& object : objects) {
            if (object && object->isExitGridMarker() && object->position >= 0 && object->position < kHexCount) {
                seeds.push_back(object->position);
            }
        }
        return seeds;
    }

    // Which walkable components the entry seeds land in (a seed on a blocked hex still "enters" via an
    // adjacent walkable hex). seedFlags is indexed by component id.
    std::vector<char> seedComponentFlags(const std::vector<int>& seeds, const std::vector<char>& blocked,
        const std::vector<int>& component, std::size_t componentCount) {
        std::vector<char> seedFlags(componentCount, 0);
        const auto mark = [&](int hex) {
            if (hex >= 0 && hex < kHexCount && !blocked[hex]) {
                seedFlags[component[hex]] = 1;
            }
        };
        for (const int seed : seeds) {
            if (seed >= 0 && seed < kHexCount && !blocked[seed]) {
                seedFlags[component[seed]] = 1;
            } else {
                for (const int neighbour : hexNeighbors(seed)) {
                    mark(neighbour);
                }
            }
        }
        return seedFlags;
    }

    // The walkable component a marker hex sits in (or, if the marker is on a blocked hex, its first
    // walkable neighbour's). -1 if it touches no walkable hex.
    int componentOf(int hex, const std::vector<char>& blocked, const std::vector<int>& component) {
        if (hex < 0 || hex >= kHexCount) {
            return -1;
        }
        if (!blocked[hex]) {
            return component[hex];
        }
        for (const int neighbour : hexNeighbors(hex)) {
            if (!blocked[neighbour]) {
                return component[neighbour];
            }
        }
        return -1;
    }

    // Per exit grid on this elevation, whether the player can walk to it from the start (same walkable
    // component). null when the player starts on a different elevation (the route is via stairs).
    ordered_json exitsJson(const Map::MapHeader& header, int elevation, const std::vector<std::shared_ptr<MapObject>>& objects,
        const std::vector<char>& blocked, const std::vector<int>& component) {
        const bool playerHere = static_cast<int>(header.player_default_elevation) == elevation;
        const int playerComponent = playerHere
            ? componentOf(static_cast<int>(header.player_default_position), blocked, component)
            : -1;
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
                const int exitComponent = componentOf(object->position, blocked, component);
                exit["reachableFromPlayerStart"] = playerComponent >= 0 && exitComponent == playerComponent;
            }
            array.push_back(exit);
        }
        return array;
    }

    // A hex reaches the player-accessible area if it (or, for a hex an object stands on, a neighbour)
    // is a walkable hex in a component the entry seeds reached.
    bool reachesEntry(int hex, const std::vector<char>& blocked, const std::vector<int>& component, const std::vector<char>& seedFlags) {
        if (hex < 0 || hex >= kHexCount) {
            return false;
        }
        if (!blocked[hex] && seedFlags[component[hex]]) {
            return true;
        }
        for (const int neighbour : hexNeighbors(hex)) {
            if (!blocked[neighbour] && seedFlags[component[neighbour]]) {
                return true;
            }
        }
        return false;
    }

    // Critters/items whose hex is cut off from the entry-reachable area.
    ordered_json orphanedObjectsJson(resource::GameResources& resources, const std::vector<std::shared_ptr<MapObject>>& objects,
        const std::vector<char>& blocked, const std::vector<int>& component, const std::vector<char>& seedFlags, std::size_t& totalOut) {
        auto array = ordered_json::array();
        std::size_t total = 0;
        for (const auto& object : objects) {
            if (!object || !isGameplayObject(*object) || object->position < 0 || object->position >= kHexCount) {
                continue;
            }
            if (reachesEntry(object->position, blocked, component, seedFlags)) {
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
        const std::vector<char> blocked = blockedMask(resources, objects);
        std::vector<int> sizes;
        std::vector<int> samples;
        const std::vector<int> component = hexComponents(blocked, sizes, samples);

        int walkable = 0;
        for (const int size : sizes) {
            walkable += size;
        }

        ordered_json entry;
        entry["elevation"] = elevation;
        entry["walkableHexes"] = walkable;
        entry["blockedHexes"] = kHexCount - walkable;

        const std::vector<int> seeds = entryHexes(header, elevation, objects);
        entry["entryPointHexes"] = static_cast<int>(seeds.size());
        if (seeds.empty()) {
            // No player start or exit grid here — this elevation is entered via stairs/elevators, which
            // the hex flood-fill doesn't model, so reachability can't be determined.
            entry["reachableHexes"] = nullptr;
            entry["orphanedObjects"] = ordered_json::array();
            entry["orphanedObjectCount"] = 0;
            entry["note"] = "no entry point on this elevation (reached via stairs/elevators, not modeled)";
            return entry;
        }

        const std::vector<char> seedFlags = seedComponentFlags(seeds, blocked, component, sizes.size());
        int reachable = 0;
        for (std::size_t id = 0; id < sizes.size(); ++id) {
            if (seedFlags[id]) {
                reachable += sizes[id];
            }
        }
        entry["reachableHexes"] = reachable;

        entry["exits"] = exitsJson(header, elevation, objects, blocked, component);

        std::size_t orphanTotal = 0;
        entry["orphanedObjects"] = orphanedObjectsJson(resources, objects, blocked, component, seedFlags, orphanTotal);
        entry["orphanedObjectCount"] = static_cast<int>(orphanTotal);
        if (orphanTotal > kMaxReported) {
            spdlog::info("reachability {} elev {}: reporting {} of {} orphaned objects", mapName, elevation, kMaxReported, orphanTotal);
        }
        return entry;
    }
} // namespace

std::vector<int> hexNeighbors(int hex) {
    using namespace hexgrid;
    static constexpr std::array<Cube, 6> kDirs = {
        { { 1, -1, 0 }, { 1, 0, -1 }, { 0, 1, -1 }, { -1, 1, 0 }, { -1, 0, 1 }, { 0, -1, 1 } }
    };
    std::vector<int> result;
    if (hex < 0 || hex >= kHexCount) {
        return result;
    }
    const Cube c = cubeOfPosition(hex);
    for (const Cube& d : kDirs) {
        const ColRow cr = cubeToOffset(c + d);
        if (cr.col >= 0 && cr.col < WIDTH && cr.row >= 0 && cr.row < HEIGHT) {
            result.push_back(cr.row * WIDTH + cr.col);
        }
    }
    return result;
}

std::vector<int> hexComponents(const std::vector<char>& blocked, std::vector<int>& sizes, std::vector<int>& samples) {
    std::vector<int> component(blocked.size(), -1);
    sizes.clear();
    samples.clear();
    for (int start = 0; start < static_cast<int>(blocked.size()); ++start) {
        if (blocked[start] || component[start] != -1) {
            continue;
        }
        const int id = static_cast<int>(sizes.size());
        int size = 0;
        std::queue<int> frontier;
        frontier.push(start);
        component[start] = id;
        while (!frontier.empty()) {
            const int hex = frontier.front();
            frontier.pop();
            ++size;
            for (const int neighbour : hexNeighbors(hex)) {
                if (!blocked[neighbour] && component[neighbour] == -1) {
                    component[neighbour] = id;
                    frontier.push(neighbour);
                }
            }
        }
        sizes.push_back(size);
        samples.push_back(start);
    }
    return component;
}

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
