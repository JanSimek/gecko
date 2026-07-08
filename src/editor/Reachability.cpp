#include "editor/Reachability.h"

#include "editor/HexGeometry.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"

#include <array>
#include <exception>
#include <queue>

namespace geck::reachability {

namespace {
    constexpr int kHexCount = hexgrid::WIDTH * hexgrid::HEIGHT;

    // A door blocks only until opened; for reachability the player can open it, so doors are passable.
    bool isDoor(resource::GameResources& resources, const MapObject& object) {
        try {
            if (const Pro* pro = resources.loadPro(object.pro_pid); pro != nullptr) {
                return pro->type() == Pro::OBJECT_TYPE::SCENERY
                    && static_cast<Pro::SCENERY_TYPE>(pro->objectSubtypeId()) == Pro::SCENERY_TYPE::DOOR;
            }
        } catch (const std::exception&) { // a missing/odd proto just isn't treated as a door
        }
        return false;
    }
} // namespace

bool blocksMovementByInstance(std::uint32_t objectType, std::uint32_t flags) {
    const bool blockingType = objectType == static_cast<std::uint32_t>(Pro::OBJECT_TYPE::CRITTER)
        || objectType == static_cast<std::uint32_t>(Pro::OBJECT_TYPE::SCENERY)
        || objectType == static_cast<std::uint32_t>(Pro::OBJECT_TYPE::WALL);
    if (!blockingType) {
        return false;
    }
    return !Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_HIDDEN)
        && !Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_BLOCK);
}

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

std::vector<char> blockedMask(resource::GameResources& resources, const std::vector<std::shared_ptr<MapObject>>& objects) {
    std::vector<char> blocked(kHexCount, 0);
    for (const auto& object : objects) {
        if (!object || object->position < 0 || object->position >= kHexCount) {
            continue;
        }
        if (!blocksMovementByInstance(object->objectType(), object->flags) || isDoor(resources, *object)) {
            continue;
        }
        blocked[object->position] = 1;
        if (Pro::hasFlag(object->flags, Pro::ObjectFlags::OBJECT_MULTIHEX)) {
            for (const int neighbour : hexNeighbors(object->position)) {
                blocked[neighbour] = 1;
            }
        }
    }
    return blocked;
}

std::vector<int> entryHexes(int playerDefaultElevation, int playerDefaultPosition, int elevation,
    const std::vector<std::shared_ptr<MapObject>>& objects) {
    std::vector<int> seeds;
    if (playerDefaultElevation == elevation) {
        seeds.push_back(playerDefaultPosition);
    }
    for (const auto& object : objects) {
        if (object && object->isExitGridMarker() && object->position >= 0 && object->position < kHexCount) {
            seeds.push_back(object->position);
        }
    }
    return seeds;
}

std::vector<char> seedComponentFlags(const std::vector<int>& seeds, const std::vector<char>& blocked,
    const std::vector<int>& component, std::size_t componentCount) {
    std::vector<char> seedFlags(componentCount, 0);
    for (const int seed : seeds) {
        if (seed >= 0 && seed < kHexCount && !blocked[seed]) {
            seedFlags[component[seed]] = 1;
        } else {
            for (const int neighbour : hexNeighbors(seed)) {
                if (!blocked[neighbour]) {
                    seedFlags[component[neighbour]] = 1;
                }
            }
        }
    }
    return seedFlags;
}

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

bool reaches(int hex, const std::vector<char>& blocked, const std::vector<int>& component, const std::vector<char>& seedFlags) {
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

ElevationResult analyzeElevation(resource::GameResources& resources, int playerDefaultElevation,
    int playerDefaultPosition, int elevation, const std::vector<std::shared_ptr<MapObject>>& objects) {
    ElevationResult result;
    result.blocked = blockedMask(resources, objects);
    std::vector<int> samples;
    result.component = hexComponents(result.blocked, result.componentSizes, samples);
    result.entrySeeds = entryHexes(playerDefaultElevation, playerDefaultPosition, elevation, objects);
    result.entryReachable = seedComponentFlags(result.entrySeeds, result.blocked, result.component, result.componentSizes.size());
    result.playerHere = playerDefaultElevation == elevation;
    if (result.playerHere) {
        result.playerStartReachable = seedComponentFlags({ playerDefaultPosition }, result.blocked, result.component, result.componentSizes.size());
    }
    return result;
}

std::vector<int> unreachableWalkableHexes(const ElevationResult& result) {
    std::vector<int> hexes;
    if (!result.hasEntryPoints()) {
        return hexes; // undetermined -> shade nothing rather than everything
    }
    for (int hex = 0; hex < kHexCount; ++hex) {
        if (!result.blocked[hex] && !result.entryReachable[result.component[hex]]) {
            hexes.push_back(hex);
        }
    }
    return hexes;
}

} // namespace geck::reachability
