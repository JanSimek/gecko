#include "cli/HexSightline.h"

#include "cli/EngineTile.h"
#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <map>
#include <memory>
#include <ostream>
#include <string>
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
        } catch (const std::exception& e) {
            // An unresolvable proto leaves the name empty, as proto_info does.
            spdlog::debug("hex_sightline: no display name for pid {}: {}", pid, e.what());
        }
        return {};
    }

    ordered_json hexJson(int hex) {
        return { { "hex", hex }, { "col", hex % enginetile::GRID_WIDTH }, { "row", hex / enginetile::GRID_WIDTH } };
    }

    ordered_json rotationJson(int rotation) {
        return { { "value", rotation }, { "name", kRotationNames[static_cast<std::size_t>(rotation)] } };
    }

    ordered_json objectJson(resource::GameResources& resources, const MapObject& object) {
        return { { "pid", std::format("0x{:08X}", object.pro_pid) }, { "name", protoName(resources, object.pro_pid) },
            { "type", Pro::typeToString(Pro::typeOfPid(object.pro_pid)) }, { "hex", object.position },
            { "flags", std::format("0x{:08X}", object.flags) } };
    }

    ordered_json tilesJson(const std::vector<int>& tiles) {
        auto array = ordered_json::array();
        for (const int tile : tiles) {
            array.push_back(tile);
        }
        return array;
    }

    // The objects on one elevation of a map, by hex, answering _obj_blocking_at for the straight-path walk.
    // obj_can_see_obj walks from the watcher, which _obj_blocking_at skips: here, the critter on the start hex.
    class BlockingScene {
    public:
        BlockingScene(Map& map, int elevation, int watcherHex) {
            auto& byElevation = map.getMapFile().map_objects;
            const auto it = byElevation.find(elevation);
            if (it == byElevation.end()) {
                return;
            }
            for (const auto& object : it->second) {
                if (object) {
                    add(*object, watcherHex);
                }
            }
        }

        // _obj_blocking_at: a blocker on the tile itself, else a multihex object on one of its six neighbours.
        int blockerAt(int tile) const {
            if (!enginetile::isValid(tile)) {
                return -1;
            }
            if (const int id = firstBlocking(tile, false); id >= 0) {
                return id;
            }
            for (int rotation = 0; rotation < enginetile::ROTATION_COUNT; ++rotation) {
                if (const int id = firstBlocking(enginetile::tileInDirection(tile, rotation, 1), true); id >= 0) {
                    return id;
                }
            }
            return -1;
        }

        bool isShootThrough(int id) const { return (object(id).flags & OBJECT_SHOOT_THRU) != 0; }
        const MapObject& object(int id) const { return *_objects[static_cast<std::size_t>(id)]; }
        const MapObject* watcher() const { return _watcher; }

    private:
        void add(const MapObject& object, int watcherHex) {
            const auto hex = static_cast<int>(object.position);
            _byHex[hex].push_back(static_cast<int>(_objects.size()));
            _objects.push_back(&object);
            if (_watcher == nullptr && hex == watcherHex && typeFromFid(object.frm_pid) == OBJ_TYPE_CRITTER) {
                _watcher = &object;
            }
        }

        int firstBlocking(int tile, bool multihexOnly) const {
            const auto it = _byHex.find(tile);
            if (it == _byHex.end()) {
                return -1;
            }
            for (const int id : it->second) {
                const MapObject& candidate = object(id);
                if ((!multihexOnly || (candidate.flags & OBJECT_MULTIHEX) != 0) && blocks(candidate)) {
                    return id;
                }
            }
            return -1;
        }

        bool blocks(const MapObject& candidate) const {
            const uint32_t type = typeFromFid(candidate.frm_pid);
            return &candidate != _watcher && (candidate.flags & OBJECT_HIDDEN) == 0 && (candidate.flags & OBJECT_NO_BLOCK) == 0
                && (type == OBJ_TYPE_CRITTER || type == OBJ_TYPE_SCENERY || type == OBJ_TYPE_WALL);
        }

        std::vector<const MapObject*> _objects;
        std::map<int, std::vector<int>> _byHex;
        const MapObject* _watcher = nullptr;
    };

    ordered_json mapSightlineJson(resource::GameResources& resources, const HexSightlineOptions& options,
        const enginetile::Camera& camera, Map& map) {
        const BlockingScene scene(map, options.elevation, options.fromHex);
        const auto path = enginetile::straightPath(
            options.fromHex, options.toHex, camera, [&scene](int tile) { return scene.blockerAt(tile); },
            options.shootThrough, [&scene](int id) { return scene.isShootThrough(id); });

        ordered_json sightline = { { "shootThrough", options.shootThrough }, { "tilesEntered", tilesJson(path.tilesEntered) },
            { "map", options.mapPath },
            { "watcher", scene.watcher() != nullptr ? objectJson(resources, *scene.watcher()) : ordered_json(nullptr) } };
        if (path.obstacleId < 0) {
            sightline["firstBlocker"] = nullptr;
            return sightline;
        }
        const MapObject& blocker = scene.object(path.obstacleId);
        ordered_json blockerJson = objectJson(resources, blocker);
        blockerJson["foundAtHex"] = path.obstacleTile;
        blockerJson["standsOnTarget"] = static_cast<int>(blocker.position) == options.toHex;
        sightline["firstBlocker"] = std::move(blockerJson);
        return sightline;
    }

    ordered_json openSightlineJson(const HexSightlineOptions& options, const enginetile::Camera& camera) {
        const auto path = enginetile::straightPath(options.fromHex, options.toHex, camera, [](int) { return -1; });
        return { { "shootThrough", options.shootThrough }, { "tilesEntered", tilesJson(path.tilesEntered) },
            { "firstBlocker", nullptr }, { "note", "no map given, so blockers were not checked" } };
    }

} // namespace

int analyzeHexSightline(resource::GameResources& resources, const HexSightlineOptions& options, std::ostream& out) {
    const int cameraHex = options.cameraHex >= 0 ? options.cameraHex : options.fromHex;
    if (!enginetile::isValid(options.fromHex) || !enginetile::isValid(options.toHex) || !enginetile::isValid(cameraHex)) {
        out << "hex_sightline: fromHex, toHex and cameraHex must be hexes in 0.." << enginetile::GRID_SIZE - 1 << "\n";
        return 2;
    }

    std::unique_ptr<Map> map;
    if (!options.mapPath.empty()) {
        std::string error;
        map = loadMap(resources, options.mapPath, &error);
        if (!map) {
            out << "hex_sightline: could not read map " << options.mapPath << ": " << error << "\n";
            return 1;
        }
    }

    const enginetile::Camera camera = enginetile::cameraCenteredOn(cameraHex);
    ordered_json root = { { "from", hexJson(options.fromHex) }, { "to", hexJson(options.toHex) },
        { "elevation", options.elevation }, { "cameraHex", cameraHex },
        { "distance", enginetile::distance(options.fromHex, options.toHex, camera) },
        { "rotationFromTo", rotationJson(enginetile::rotationTo(options.fromHex, options.toHex, camera)) },
        { "rotationToFrom", rotationJson(enginetile::rotationTo(options.toHex, options.fromHex, camera)) } };
    if (options.facing >= 0 && options.facing < enginetile::ROTATION_COUNT) {
        root["facing"] = rotationJson(options.facing);
        root["facing"]["targetInFrontalArc"] = enginetile::inFrontalArc(options.fromHex, options.facing, options.toHex, camera);
    }
    root["sightline"] = map ? mapSightlineJson(resources, options, camera, *map) : openSightlineJson(options, camera);

    // Proto names are CP-1252 game text, so emit invalid UTF-8 as replacement characters rather than throwing.
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
