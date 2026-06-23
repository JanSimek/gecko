#include "MapWriter.h"

#include <spdlog/spdlog.h>

#include "format/pro/Pro.h"
#include "format/map/MapScript.h"
#include "format/map/MapObjectFields.h"
#include "format/map/MapScriptFields.h"
#include "format/map/Tile.h"

namespace geck {
namespace {

    const char* objectTypeName(uint32_t objectTypeId) {
        switch (static_cast<Pro::OBJECT_TYPE>(objectTypeId)) {
            case Pro::OBJECT_TYPE::ITEM:
                return "ITEM";
            case Pro::OBJECT_TYPE::CRITTER:
                return "CRITTER";
            case Pro::OBJECT_TYPE::SCENERY:
                return "SCENERY";
            case Pro::OBJECT_TYPE::WALL:
                return "WALL";
            case Pro::OBJECT_TYPE::TILE:
                return "TILE";
            case Pro::OBJECT_TYPE::MISC:
                return "MISC";
            default:
                return "unknown";
        }
    }

} // namespace

bool MapWriter::write(const Map::MapFile& map) {
    try {
        if (!isOpen()) {
            throw WriteException("File is not open for writing", getPath());
        }

        auto& utils = getBinaryUtils();
        spdlog::info("Saving map {} version {}", map.header.filename, map.header.version);

        // Write map header
        utils.writeWithLog(map.header.version, "map version");
        utils.writeFixedString(map.header.filename, Map::FILENAME_LENGTH);

        utils.writeWithLog(map.header.player_default_position, "player default position");
        utils.writeWithLog(map.header.player_default_elevation, "player default elevation");
        utils.writeWithLog(map.header.player_default_orientation, "player default orientation");

        utils.writeWithLog(map.header.num_local_vars, "number of local variables");
        utils.writeWithLog(map.header.script_id, "script ID");

        utils.writeWithLog(map.header.flags, "flags");
        utils.writeWithLog(map.header.darkness, "darkness");
        utils.writeWithLog(map.header.num_global_vars, "number of global variables");
        utils.writeWithLog(map.header.map_id, "map ID");
        utils.writeWithLog(map.header.timestamp, "timestamp");

        // Write unused header bytes
        constexpr int MAP_HEADER_UNUSED_BYTES = 44;
        utils.writePadding(MAP_HEADER_UNUSED_BYTES * sizeof(uint32_t));

        // Write global variables
        for (const auto& var : map.map_global_vars) {
            utils.writeBE32(var);
        }
        spdlog::debug("Wrote {} global variables", map.map_global_vars.size());

        // Write local variables
        for (const auto& var : map.map_local_vars) {
            utils.writeBE32(var);
        }
        spdlog::debug("Wrote {} local variables", map.map_local_vars.size());

        // Write tiles
        int elevationsWritten = 0;
        for (int elevation = 0; elevation < Map::ELEVATION_COUNT; ++elevation) {
            if (!Map::elevationIsPresent(map.header.flags, elevation)) {
                continue;
            }
            ++elevationsWritten;

            auto it = map.tiles.find(elevation);
            if (it != map.tiles.end() && it->second.size() == Map::TILES_PER_ELEVATION) {
                for (const auto& tile : it->second) {
                    utils.writeBE16(tile.getRoof());
                    utils.writeBE16(tile.getFloor());
                }
            } else {
                // Elevation flagged present but without a full tile grid in
                // memory: emit an empty grid so the block size matches what the
                // engine expects to read for this elevation.
                for (unsigned i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
                    utils.writeBE16(Map::EMPTY_TILE);
                    utils.writeBE16(Map::EMPTY_TILE);
                }
            }
        }
        spdlog::debug("Wrote tiles for {} elevations", elevationsWritten);

        // Write scripts
        for (const auto& script_section : map.map_scripts) {
            uint32_t number_of_scripts = static_cast<uint32_t>(script_section.size());
            utils.writeWithLog(number_of_scripts, "number of scripts in section");

            if (number_of_scripts == 0) {
                continue;
            }

            int current_sequence = 0;
            uint32_t check = 0;

            // Round number of scripts to be divisible by 16
            int remainder = number_of_scripts % 16;
            uint32_t scripts_in_section = (remainder == 0 ? number_of_scripts : number_of_scripts + 16 - remainder);

            for (uint32_t i = 0; i < scripts_in_section; i++) {
                if (i < number_of_scripts) {
                    writeScript(script_section.at(i));
                    current_sequence++;
                    check++;
                } else {
                    // Fill the rest with empty scripts
                    for (int j = 0; j < 16; j++) {
                        utils.writeBE32(0xCC); // empty MapScript without additional fields for spatial/timer scripts
                    }
                }

                if (i % 16 == 15) { // check after every batch
                    utils.writeWithLog(current_sequence, "current sequence");
                    utils.writeBE32(0); // unknown
                    current_sequence = 0;
                }
            }

            if (check != number_of_scripts) {
                throw ValidationException("Script count mismatch", getPath(), "scripts");
            }
        }
        spdlog::debug("Wrote script sections for {} script types", Map::SCRIPT_SECTIONS);

        size_t total_objects = 0;
        for (int elev = 0; elev < Map::ELEVATION_COUNT; ++elev) {
            auto it = map.map_objects.find(elev);
            if (it != map.map_objects.end()) {
                total_objects += it->second.size();
            }
        }

        utils.writeWithLog(static_cast<uint32_t>(total_objects), "total objects on map");

        const std::vector<std::shared_ptr<MapObject>> emptyElevation;
        for (int elev = 0; elev < Map::ELEVATION_COUNT; ++elev) {
            auto it = map.map_objects.find(elev);
            const auto& objectsOnElevation = (it != map.map_objects.end()) ? it->second : emptyElevation;
            utils.writeWithLog(static_cast<uint32_t>(objectsOnElevation.size()), "objects on elevation " + std::to_string(elev));

            // TODO: sort objects by their position for better loading performance
            for (const auto& object : objectsOnElevation) {
                writeObject(*object);
            }
        }

        // (probably not relevant anymore) FIXME: some maps (artemple.map, kladwtwn.map, all?) contain 2x extra 0x000000 at the end of the file
        // without them kladwtnwn.map crashes Fallout 2; however F2_Dims_Mapper doesn't seem to add them (?)
        // utils.writeBE32(0);
        // utils.writeBE32(0);

        utils.flush();
        spdlog::debug("Successfully wrote map file: {} ({} bytes)", getPath().filename().string(), getBytesWritten());
        return true;

    } catch (const FileWriterException& e) {
        spdlog::error("Failed to write map file {}: {}", getPath().string(), e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Unexpected error writing map file {}: {}", getPath().string(), e.what());
        return false;
    }
}

void MapWriter::writeScript(const MapScript& script) {
    auto& utils = getBinaryUtils();

    utils.writeBE32(script.pid);
    utils.writeBE32(script.next_script);

    switch (MapScript::fromPid(script.pid)) {
        case MapScript::ScriptType::SYSTEM:
            break;
        case MapScript::ScriptType::SPATIAL:
            utils.writeBE32(script.timer);
            utils.writeBE32(script.spatial_radius);
            break;
        case MapScript::ScriptType::TIMER:
            utils.writeBE32(script.timer);
            break;
        case MapScript::ScriptType::ITEM:
        case MapScript::ScriptType::CRITTER:
            break;
        default:
            throw ValidationException("Unknown script PID type", getPath(),
                "script PID " + std::to_string((script.pid & 0xFF000000) >> 24));
    }

    // Fixed trailer (layout shared with MapReader via the visitor); each field is emitted as a
    // big-endian 32-bit word.
    visitMapScriptTrailerFields(script, [&utils]<class T>(const T& field) {
        utils.writeBE32(static_cast<uint32_t>(field));
    });
}

void MapWriter::writeObject(const MapObject& object) {
    auto& utils = getBinaryUtils();

    // Write the common-field block (layout shared with MapReader via the visitor).
    // Every field is emitted as a big-endian 32-bit word.
    visitMapObjectCommonFields(object, [&utils](const auto& field) {
        utils.writeBE32(static_cast<uint32_t>(field));
    });

    uint32_t objectTypeId = object.objectType();

    spdlog::debug("Writing object type: {}", objectTypeName(objectTypeId));

    auto object_type = static_cast<Pro::OBJECT_TYPE>(objectTypeId);

    switch (object_type) {
        case Pro::OBJECT_TYPE::ITEM: {
            auto pro = _loadProCallback(object.pro_pid);
            if (!pro) {
                throw ValidationException("Cannot load PRO file for object", getPath(), "pro_pid " + std::to_string(object.pro_pid));
            }

            uint32_t subtype_id = pro->objectSubtypeId();
            switch (static_cast<Pro::ITEM_TYPE>(subtype_id)) {
                case Pro::ITEM_TYPE::AMMO:
                case Pro::ITEM_TYPE::MISC:
                    utils.writeBE32(object.ammo); // bullets/charges
                    break;
                case Pro::ITEM_TYPE::KEY:
                    utils.writeBE32(object.keycode);
                    break;
                case Pro::ITEM_TYPE::WEAPON:
                    utils.writeBE32(object.ammo);     // ammo count
                    utils.writeBE32(object.ammo_pid); // ammo type PID
                    break;
                case Pro::ITEM_TYPE::ARMOR:
                case Pro::ITEM_TYPE::CONTAINER:
                case Pro::ITEM_TYPE::DRUG:
                    // No additional data for these item types
                    break;
                default:
                    throw ValidationException("Unknown item subtype", getPath(),
                        "item subtype " + std::to_string(subtype_id));
            }
        } break;
        case Pro::OBJECT_TYPE::CRITTER:
            utils.writeBE32(object.player_reaction); // reaction to player - saves only
            utils.writeBE32(object.current_mp);      // current mp - saves only
            utils.writeBE32(object.combat_results);  // combat results - saves only
            utils.writeBE32(object.dmg_last_turn);   // damage last turn - saves only
            utils.writeBE32(object.ai_packet);       // AI packet
            utils.writeBE32(object.group_id);        // team/group ID
            utils.writeBE32(object.who_hit_me);      // who hit me - saves only
            utils.writeBE32(object.current_hp);      // current hit points
            utils.writeBE32(object.current_rad);     // current radiation
            utils.writeBE32(object.current_poison);  // current poison level
            break;

        case Pro::OBJECT_TYPE::SCENERY: {
            auto pro = _loadProCallback(object.pro_pid);
            if (!pro) {
                throw ValidationException("Cannot load PRO file for scenery object", getPath(), "pro_pid " + std::to_string(object.pro_pid));
            }

            uint32_t subtype_id = pro->objectSubtypeId();
            switch (static_cast<Pro::SCENERY_TYPE>(subtype_id)) {
                case Pro::SCENERY_TYPE::LADDER_TOP:
                case Pro::SCENERY_TYPE::LADDER_BOTTOM:
                    utils.writeBE32(object.map);
                    utils.writeBE32(object.elevhex);
                    break;
                case Pro::SCENERY_TYPE::STAIRS:
                    // Note: for ladders and stairs, map and elev+hex fields are in different order
                    utils.writeBE32(object.elevhex);
                    utils.writeBE32(object.map);
                    break;
                case Pro::SCENERY_TYPE::ELEVATOR:
                    utils.writeBE32(object.elevtype);  // elevator type
                    utils.writeBE32(object.elevlevel); // current level
                    break;
                case Pro::SCENERY_TYPE::DOOR:
                    utils.writeBE32(object.walkthrough);
                    break;
                case Pro::SCENERY_TYPE::GENERIC:
                    // No additional data for generic scenery
                    break;
                default:
                    throw ValidationException("Unknown scenery subtype", getPath(),
                        "scenery subtype " + std::to_string(subtype_id));
            }
        } break;
        case Pro::OBJECT_TYPE::WALL:
        case Pro::OBJECT_TYPE::TILE:
            break;
        case Pro::OBJECT_TYPE::MISC:
            if (object.isExitGridMarker()) {
                utils.writeBE32(object.exit_map);
                utils.writeBE32(object.exit_position);
                utils.writeBE32(object.exit_elevation);
                utils.writeBE32(object.exit_orientation);
            }
            break;
        default:
            throw ValidationException("Unknown object type", getPath(),
                "object type " + std::to_string(objectTypeId));
    }

    // Write inventory objects if any
    if (object.objects_in_inventory > 0) {
        for (const auto& invobj : object.inventory) {
            if (!invobj) {
                throw CorruptDataException("Null inventory object", getPath(), "inventory");
            }
            utils.writeBE32(invobj->amount);
            writeObject(*invobj); // Recursive call for inventory objects
        }
    }
}

} // namespace geck
