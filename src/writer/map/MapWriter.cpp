#include "MapWriter.h"

#include <spdlog/spdlog.h>

#include "../../format/pro/Pro.h"
#include "../../format/map/MapObject.h"
#include "../../format/map/Tile.h"

#include "../../editor/helper/ObjectHelper.h"

namespace geck {

bool MapWriter::write(const Map::MapFile& map) {
    if (!stream.is_open()) {
        spdlog::error("Cannot save map. File {} is not open for writing.", path.string());
        return false;
    }

    spdlog::info("Saving map {} version {}", map.header.filename, map.header.version);

    write_be_32(map.header.version);

    stream.write(map.header.filename.c_str(), Map::FILENAME_LENGTH);

    write_be_32(map.header.player_default_position);
    write_be_32(map.header.player_default_elevation);
    write_be_32(map.header.player_default_orientation);

    write_be_32(map.header.num_local_vars);
    write_be_32(map.header.script_id);

    write_be_32(map.header.flags);
    write_be_32(map.header.darkness);
    write_be_32(map.header.num_global_vars);
    write_be_32(map.header.map_id);
    write_be_32(map.header.timestamp);

    constexpr int MAP_HEADER_UNUSED_BYTES = 44;
    for (int i = 0; i < MAP_HEADER_UNUSED_BYTES; i++) {
        write_be_32(0); // unused bytes
    }

    for (const auto& var : map.map_global_vars) {
        write_be_32(var);
    }

    for (const auto& var : map.map_local_vars) {
        write_be_32(var);
    }

    for (const auto& elevation : map.tiles) {
        for (const auto& tile : elevation.second) {
            write_be_16(tile.getRoof());
            write_be_16(tile.getFloor());
        }
    }

    // Scripts

    for (const auto& script_section : map.map_scripts) {

        uint32_t number_of_scripts = script_section.size();
        write_be_32(number_of_scripts);

        if (number_of_scripts == 0) {
            continue;
        }

        int current_sequence = 0;
        uint32_t check = 0;

        // round number of script to be divisible by 16
        int remainder = number_of_scripts % 16;
        uint32_t scripts_in_section = (remainder == 0 ? number_of_scripts : number_of_scripts + 16 - remainder);

        for (uint32_t i = 0; i < scripts_in_section; i++) {

            if (i < number_of_scripts) {
                writeScript(script_section.at(i));
                current_sequence++;
                check++;
            } else { // fill the rest with empty scripts
                for (int j = 0; j < 16; j++) {
                    write_be_32(0xCC); // empty MapScript without additional fields for spatial/timer scripts
                }
            }

            if (i % 16 == 15) { // check after every batch
                write_be_32(current_sequence);
                write_be_32(0); // unknown
                current_sequence = 0;
            }
        }

        if (check != number_of_scripts) {
            spdlog::error("Check {} doesn't match number of scripts {} in section", check, number_of_scripts);
        }
    }

    // Objects

    long total_objects = 0;
    for (size_t elev = 0; elev < map.map_objects.size(); elev++) {
        total_objects += map.map_objects.at(elev).size();
    }

    write_be_32(total_objects); // Total number of object on this map

    for (size_t elev = 0; elev < map.map_objects.size(); elev++) {

        auto objectsOnElevation = map.map_objects.at(elev).size();
        write_be_32(objectsOnElevation);

        // TODO: sort objects by their position
        for (size_t i = 0; i < objectsOnElevation; i++) {
            const auto& object = map.map_objects.at(elev)[i];
            writeObject(*object);
        }
    }

    // FIXME: some maps (artemple.map, kladwtwn.map, all?) contain 2x extra 0x000000 at the end of the file
    // without them kladwtnwn.map crashes Fallout 2; however F2_Dims_Mapper doesn't seem to add them (?)
    //write_be_32(0);
    //write_be_32(0);

    return true;
}

void MapWriter::writeScript(const MapScript& script) {
    write_be_32(script.pid);
    write_be_32(script.next_script);

    switch (MapScript::fromPid(script.pid)) {
        case MapScript::ScriptType::SYSTEM:
            break;
        case MapScript::ScriptType::SPATIAL:
            write_be_32(script.timer);
            write_be_32(script.spatial_radius);
            break;
        case MapScript::ScriptType::TIMER:
            write_be_32(script.timer);
            break;
        case MapScript::ScriptType::ITEM:
        case MapScript::ScriptType::CRITTER:
            break;
        default:
            spdlog::error("Unknown script PID = {}", (script.pid & 0xFF000000) >> 24);
            break;
    }

    write_be_32(script.flags);
    write_be_32(script.script_id);
    write_be_32(script.unknown5);
    write_be_32(script.script_oid);
    write_be_32(script.local_var_offset);
    write_be_32(script.local_var_count);
    write_be_32(script.unknown9);
    write_be_32(script.unknown10);
    write_be_32(script.unknown11);
    write_be_32(script.unknown12);
    write_be_32(script.unknown13);
    write_be_32(script.unknown14);
    write_be_32(script.unknown15);
    write_be_32(script.unknown16);
}

void MapWriter::writeObject(const MapObject& object) {

    write_be_32(object.unknown0);
    write_be_32(object.position);
    write_be_32(object.x);
    write_be_32(object.y);
    write_be_32(object.sx);
    write_be_32(object.sy);
    write_be_32(object.frame_number);
    write_be_32(object.direction);
    write_be_32(object.frm_pid);
    write_be_32(object.flags);
    write_be_32(object.elevation);
    write_be_32(object.pro_pid);
    write_be_32(object.critter_index);
    write_be_32(object.light_radius);
    write_be_32(object.light_intensity);
    write_be_32(object.outline_color);
    write_be_32(object.map_scripts_pid);
    write_be_32(object.script_id);
    write_be_32(object.objects_in_inventory);
    write_be_32(object.max_inventory_size);
    write_be_32(object.unknown10);
    write_be_32(object.unknown11);

    uint32_t objectTypeId = object.pro_pid >> 24;
    uint32_t objectId = 0x00FFFFFF & object.pro_pid;

    spdlog::info("Saving object {}", ObjectHelper::objectTypeFromId(objectTypeId));

    auto object_type = static_cast<Pro::OBJECT_TYPE>(objectTypeId);

    switch (object_type) {
        case Pro::OBJECT_TYPE::ITEM: {

            auto pro = _loadProCallback(object.pro_pid);

            uint32_t subtype_id = pro->objectSubtypeId();
            switch (static_cast<Pro::ITEM_TYPE>(subtype_id)) {
                case Pro::ITEM_TYPE::AMMO:    // ammo
                case Pro::ITEM_TYPE::MISC:    // charges - have strangely high values, or negative.
                    write_be_32(object.ammo); // bullets
                    break;
                case Pro::ITEM_TYPE::KEY:
                    write_be_32(object.keycode); // keycode = -1 in all maps. saves only? ignore for now
                    break;
                case Pro::ITEM_TYPE::WEAPON:
                    write_be_32(object.ammo);     // ammo
                    write_be_32(object.ammo_pid); // ammo pid
                    break;
                case Pro::ITEM_TYPE::ARMOR:
                case Pro::ITEM_TYPE::CONTAINER:
                case Pro::ITEM_TYPE::DRUG:
                    break;
                default:
                    throw std::runtime_error{ "Unknown item type " + std::to_string(objectTypeId) };
            }
        } break;
        case Pro::OBJECT_TYPE::CRITTER:
            write_be_32(object.player_reaction); // reaction to player - saves only
            write_be_32(object.current_mp);      // current mp - saves only
            write_be_32(object.combat_results);  // combat results - saves only
            write_be_32(object.dmg_last_turn);   // damage last turn - saves only
            write_be_32(object.ai_packet);       // AI packet - is it different from .pro? well, it can be
            write_be_32(object.group_id);        // team - always 1? saves only?
            write_be_32(object.who_hit_me);      // who hit me - saves only
            write_be_32(object.current_hp);      // hit points - saves only, otherwise = value from .pro
            write_be_32(object.current_rad);     // rad - always 0 - saves only
            write_be_32(object.current_poison);  // poison - always 0 - saves only

            break;

        case Pro::OBJECT_TYPE::SCENERY: {

            auto pro = _loadProCallback(object.pro_pid);

            uint32_t subtype_id = pro->objectSubtypeId();
            switch (static_cast<Pro::SCENERY_TYPE>(subtype_id)) {
                case Pro::SCENERY_TYPE::LADDER_TOP:
                case Pro::SCENERY_TYPE::LADDER_BOTTOM:
                    write_be_32(object.map);
                    write_be_32(object.elevhex);
                    break;
                case Pro::SCENERY_TYPE::STAIRS:
                    // looks like for ladders and stairs map and elev+hex fields in the different order
                    write_be_32(object.elevhex);
                    write_be_32(object.map);
                    break;
                case Pro::SCENERY_TYPE::ELEVATOR:
                    write_be_32(object.elevtype);  // elevator type - sometimes -1
                    write_be_32(object.elevlevel); // current level - sometimes -1
                    break;
                case Pro::SCENERY_TYPE::DOOR:
                    write_be_32(object.walkthrough);
                    break;
                case Pro::SCENERY_TYPE::GENERIC:
                    break;
                default:
                    throw std::runtime_error{ "Unknown scenery type: " + std::to_string(subtype_id) };
            }
        } break;
        case Pro::OBJECT_TYPE::WALL:
        case Pro::OBJECT_TYPE::TILE:
            break;
        case Pro::OBJECT_TYPE::MISC:

            switch (objectId) {
                case 12:
                    break;
                // Exit Grids
                case 16:
                case 17:
                case 18:
                case 19:
                case 20:
                case 21:
                case 22:
                case 23:
                default:
                    write_be_32(object.exit_map);
                    write_be_32(object.exit_position);
                    write_be_32(object.exit_elevation);
                    write_be_32(object.exit_orientation);
                    break;
            }
            break;
        default:
            throw std::runtime_error{ "Unknown object type: " + std::to_string(objectTypeId) };
    }

    if (object.objects_in_inventory > 0) {

        for (const auto& invobj : object.inventory) {
            write_be_32(invobj->amount);
            writeObject(*invobj);
        }
    }
}

} // namespace geck
