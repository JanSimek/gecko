#include "MapWriter.h"

#include <spdlog/spdlog.h>

#include "../../format/pro/Pro.h"
#include "../../format/map/MapScript.h"
#include "../../format/map/Tile.h"
#include "../../editor/helper/ObjectHelper.h"

namespace geck {

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
        for (const auto& elevation : map.tiles) {
            for (const auto& tile : elevation.second) {
                utils.writeBE16(tile.getRoof());
                utils.writeBE16(tile.getFloor());
            }
        }
        spdlog::debug("Wrote tiles for {} elevations", map.tiles.size());

        // Write scripts
        for (const auto& script_section : map.map_scripts) {
            uint32_t number_of_scripts = script_section.size();
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

        // Write objects
        size_t total_objects = 0;
        for (size_t elev = 0; elev < map.map_objects.size(); elev++) {
            total_objects += map.map_objects.at(elev).size();
        }

        utils.writeWithLog(static_cast<uint32_t>(total_objects), "total objects on map");

        for (size_t elev = 0; elev < map.map_objects.size(); elev++) {
            auto objectsOnElevation = map.map_objects.at(elev).size();
            utils.writeWithLog(static_cast<uint32_t>(objectsOnElevation), "objects on elevation " + std::to_string(elev));

            // TODO: sort objects by their position for better loading performance
            for (size_t i = 0; i < objectsOnElevation; i++) {
                const auto& object = map.map_objects.at(elev)[i];
                writeObject(*object);
            }
        }

        // FIXME: some maps (artemple.map, kladwtwn.map, all?) contain 2x extra 0x000000 at the end of the file
        // without them kladwtnwn.map crashes Fallout 2; however F2_Dims_Mapper doesn't seem to add them (?)
        // utils.writeBE32(0);
        // utils.writeBE32(0);

        utils.flush();
        spdlog::info("Successfully wrote map file: {} ({} bytes)", getPath().filename().string(), getBytesWritten());
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

    utils.writeBE32(script.flags);
    utils.writeBE32(script.script_id);
    utils.writeBE32(script.unknown5);
    utils.writeBE32(script.script_oid);
    utils.writeBE32(script.local_var_offset);
    utils.writeBE32(script.local_var_count);
    utils.writeBE32(script.unknown9);
    utils.writeBE32(script.unknown10);
    utils.writeBE32(script.unknown11);
    utils.writeBE32(script.unknown12);
    utils.writeBE32(script.unknown13);
    utils.writeBE32(script.unknown14);
    utils.writeBE32(script.unknown15);
    utils.writeBE32(script.unknown16);
}

void MapWriter::writeObject(const MapObject& object) {
    auto& utils = getBinaryUtils();

    // Write basic object fields
    utils.writeBE32(object.unknown0);
    utils.writeBE32(object.position);
    utils.writeBE32(object.x);
    utils.writeBE32(object.y);
    utils.writeBE32(object.sx);
    utils.writeBE32(object.sy);
    utils.writeBE32(object.frame_number);
    utils.writeBE32(object.direction);
    utils.writeBE32(object.frm_pid);
    utils.writeBE32(object.flags);
    utils.writeBE32(object.elevation);
    utils.writeBE32(object.pro_pid);
    utils.writeBE32(object.critter_index);
    utils.writeBE32(object.light_radius);
    utils.writeBE32(object.light_intensity);
    utils.writeBE32(object.outline_color);
    utils.writeBE32(object.map_scripts_pid);
    utils.writeBE32(object.script_id);
    utils.writeBE32(object.objects_in_inventory);
    utils.writeBE32(object.max_inventory_size);
    utils.writeBE32(object.unknown10);
    utils.writeBE32(object.unknown11);

    uint32_t objectTypeId = object.pro_pid >> 24;
    uint32_t objectId = 0x00FFFFFF & object.pro_pid;

    spdlog::debug("Writing object type: {}", ObjectHelper::objectTypeFromId(objectTypeId));

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
            switch (objectId) {
                case 12:
                    // No additional data for misc object ID 12
                    break;
                // Exit Grids (16-23)
                case 16:
                case 17:
                case 18:
                case 19:
                case 20:
                case 21:
                case 22:
                case 23:
                default:
                    // Write exit information for exit grids
                    utils.writeBE32(object.exit_map);
                    utils.writeBE32(object.exit_position);
                    utils.writeBE32(object.exit_elevation);
                    utils.writeBE32(object.exit_orientation);
                    break;
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
