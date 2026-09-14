#include "cli/HexSightline.h"

#include "cli/EngineTile.h"
#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <ostream>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    // fallout2-ce obj_types.h Rotation.
    constexpr std::array<const char*, enginetile::ROTATION_COUNT> kRotationNames{
        "ROTATION_NE", "ROTATION_E", "ROTATION_SE", "ROTATION_SW", "ROTATION_W", "ROTATION_NW"
    };

    // fallout2-ce obj_types.h ObjectFlags / ObjectType.
    constexpr uint32_t OBJECT_HIDDEN = 0x01;
    constexpr uint32_t OBJECT_NO_BLOCK = 0x10;
    constexpr uint32_t OBJECT_MULTIHEX = 0x800;
    constexpr uint32_t OBJECT_SHOOT_THRU = 0x80000000;
    constexpr uint32_t OBJ_TYPE_CRITTER = 1;
    constexpr uint32_t OBJ_TYPE_SCENERY = 2;
    constexpr uint32_t OBJ_TYPE_WALL = 3;

    // objectTypeFromFid: the blocking check classifies by art, not by proto.
    uint32_t typeFromFid(uint32_t fid) {
        return (fid >> 24) & 0x0F;
    }

    std::string protoName(resource::GameResources& resources, uint32_t pid) {
        try {
            if (const Pro* pro = resources.loadPro(pid); pro != nullptr) {
                if (Msg* msg = ProHelper::msgFile(resources, pro->type()); msg != nullptr) {
                    return msg->message(pro->header.message_id).text;
                }
            }
        } catch (const std::exception&) {
            // An unresolvable proto leaves the name empty, as proto_info does.
        }
        return {};
    }

    ordered_json hexJson(int hex) {
        return { { "hex", hex }, { "col", hex % enginetile::GRID_WIDTH }, { "row", hex / enginetile::GRID_WIDTH } };
    }

    ordered_json objectJson(resource::GameResources& resources, const MapObject& object) {
        return { { "pid", std::format("0x{:08X}", object.pro_pid) }, { "name", protoName(resources, object.pro_pid) },
            { "type", Pro::typeToString(Pro::typeOfPid(object.pro_pid)) }, { "hex", object.position },
            { "flags", std::format("0x{:08X}", object.flags) } };
    }

} // namespace

int analyzeHexSightline(resource::GameResources& resources, const HexSightlineOptions& options, std::ostream& out) {
    if (!enginetile::isValid(options.fromHex) || !enginetile::isValid(options.toHex)) {
        out << "hex_sightline: fromHex and toHex must be hexes in 0.." << enginetile::GRID_SIZE - 1 << "\n";
        return 2;
    }
    const int cameraHex = options.cameraHex >= 0 ? options.cameraHex : options.fromHex;
    if (!enginetile::isValid(cameraHex)) {
        out << "hex_sightline: cameraHex must be a hex in 0.." << enginetile::GRID_SIZE - 1 << "\n";
        return 2;
    }
    const enginetile::Camera camera = enginetile::cameraCenteredOn(cameraHex);

    const int rotationForward = enginetile::rotationTo(options.fromHex, options.toHex, camera);
    const int rotationBack = enginetile::rotationTo(options.toHex, options.fromHex, camera);

    ordered_json root = { { "from", hexJson(options.fromHex) }, { "to", hexJson(options.toHex) },
        { "elevation", options.elevation }, { "cameraHex", cameraHex },
        { "distance", enginetile::distance(options.fromHex, options.toHex, camera) },
        { "rotationFromTo", { { "value", rotationForward }, { "name", kRotationNames[static_cast<std::size_t>(rotationForward)] } } },
        { "rotationToFrom", { { "value", rotationBack }, { "name", kRotationNames[static_cast<std::size_t>(rotationBack)] } } } };

    if (options.facing >= 0 && options.facing < enginetile::ROTATION_COUNT) {
        root["facing"] = { { "value", options.facing }, { "name", kRotationNames[static_cast<std::size_t>(options.facing)] },
            { "targetInFrontalArc", enginetile::inFrontalArc(options.fromHex, options.facing, options.toHex, camera) } };
    }

    // Objects on the requested elevation, by hex, so the blocker check can look at a tile and its six
    // neighbours as _obj_blocking_at does.
    std::unique_ptr<Map> map;
    std::vector<const MapObject*> objects;
    std::map<int, std::vector<int>> objectsByHex;
    const MapObject* watcher = nullptr;
    if (!options.mapPath.empty()) {
        std::string error;
        map = loadMap(resources, options.mapPath, &error);
        if (!map) {
            out << "hex_sightline: could not read map " << options.mapPath << ": " << error << "\n";
            return 1;
        }
        const auto& byElevation = map->getMapFile().map_objects;
        if (const auto it = byElevation.find(options.elevation); it != byElevation.end()) {
            for (const auto& object : it->second) {
                if (!object) {
                    continue;
                }
                objectsByHex[static_cast<int>(object->position)].push_back(static_cast<int>(objects.size()));
                objects.push_back(object.get());
                // obj_can_see_obj walks from the watcher, which _obj_blocking_at skips: the critter on fromHex.
                if (watcher == nullptr && static_cast<int>(object->position) == options.fromHex
                    && typeFromFid(object->frm_pid) == OBJ_TYPE_CRITTER) {
                    watcher = object.get();
                }
            }
        }
    }

    auto blocksAt = [&](int id) {
        const MapObject& object = *objects[static_cast<std::size_t>(id)];
        const uint32_t type = typeFromFid(object.frm_pid);
        return &object != watcher && (object.flags & OBJECT_HIDDEN) == 0 && (object.flags & OBJECT_NO_BLOCK) == 0
            && (type == OBJ_TYPE_CRITTER || type == OBJ_TYPE_SCENERY || type == OBJ_TYPE_WALL);
    };
    auto blockerAt = [&](int tile) -> int {
        if (objects.empty() || !enginetile::isValid(tile)) {
            return -1;
        }
        if (const auto it = objectsByHex.find(tile); it != objectsByHex.end()) {
            for (const int id : it->second) {
                if (blocksAt(id)) {
                    return id;
                }
            }
        }
        for (int rotation = 0; rotation < enginetile::ROTATION_COUNT; ++rotation) {
            const int neighbour = enginetile::tileInDirection(tile, rotation, 1);
            if (!enginetile::isValid(neighbour)) {
                continue;
            }
            if (const auto it = objectsByHex.find(neighbour); it != objectsByHex.end()) {
                for (const int id : it->second) {
                    if ((objects[static_cast<std::size_t>(id)]->flags & OBJECT_MULTIHEX) != 0 && blocksAt(id)) {
                        return id;
                    }
                }
            }
        }
        return -1;
    };
    auto isShootThrough = [&](int id) { return (objects[static_cast<std::size_t>(id)]->flags & OBJECT_SHOOT_THRU) != 0; };

    const auto path = enginetile::straightPath(options.fromHex, options.toHex, camera, blockerAt, options.shootThrough,
        isShootThrough);

    auto tiles = ordered_json::array();
    for (const int tile : path.tilesEntered) {
        tiles.push_back(tile);
    }
    ordered_json sightline = { { "shootThrough", options.shootThrough }, { "tilesEntered", std::move(tiles) } };
    if (map) {
        sightline["map"] = options.mapPath;
        sightline["watcher"] = watcher != nullptr ? objectJson(resources, *watcher) : ordered_json(nullptr);
        if (path.obstacleId >= 0) {
            const MapObject& blocker = *objects[static_cast<std::size_t>(path.obstacleId)];
            ordered_json blockerJson = objectJson(resources, blocker);
            blockerJson["foundAtHex"] = path.obstacleTile;
            blockerJson["standsOnTarget"] = static_cast<int>(blocker.position) == options.toHex;
            sightline["firstBlocker"] = std::move(blockerJson);
        } else {
            sightline["firstBlocker"] = nullptr;
        }
    } else {
        sightline["firstBlocker"] = nullptr;
        sightline["note"] = "no map given, so blockers were not checked";
    }
    root["sightline"] = std::move(sightline);

    out << root.dump(2) << "\n";
    return 0;
}

} // namespace geck::cli
