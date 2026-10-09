#include "MapReader.h"

#include <spdlog/spdlog.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "format/map/Map.h"
#include "format/map/MapObjectFields.h"
#include "format/map/MapScriptFields.h"
#include "format/msg/Msg.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "reader/lst/LstReader.h"

// Windows defines INTERFACE macro in some headers which conflicts with our enum value
#ifdef INTERFACE
#undef INTERFACE
#endif

namespace geck {

MapReader::MapReader(std::function<Pro*(uint32_t)> proLoadCallback)
    : _proLoadCallback(proLoadCallback) {
}

std::unique_ptr<MapObject> MapReader::readMapObject() {
    auto object = std::make_unique<MapObject>();

    // Read the common-field block (layout shared with MapWriter via the visitor).
    // Every field is a big-endian 32-bit word; the field's own type (signed or
    // unsigned) decides how those bits are interpreted on assignment.
    visitMapObjectCommonFields(*object, [this](auto& field) {
        field = static_cast<std::remove_reference_t<decltype(field)>>(read_be_u32());
    });

    const ObjectType objectType = object->objectType();

    auto pro = _proLoadCallback(object->pro_pid);

    // ITEM and SCENERY read a type-specific tail whose LENGTH depends on the proto's subtype, so a
    // missing proto is not a cosmetic gap: guessing the tail would desynchronise the stream and
    // corrupt every object after it. Fail explicitly and name the PID (per the engine-data rule —
    // no substitute values), so the answer is "this proto is not mounted", not a silent bad parse.
    if (pro == nullptr
        && (objectType == ObjectType::Item
            || objectType == ObjectType::Scenery)) {
        throw ParseException("proto " + std::to_string(object->pro_pid) + " (type " + std::to_string(static_cast<int>(objectType))
                + ", index " + std::to_string(object->pid().protoId()) + ") could not be loaded, so its object tail length is unknown",
            _path, _stream.position());
    }

    switch (objectType) {
        case ObjectType::Item: {
            uint32_t subtype_id = pro->objectSubtypeId();
            switch (static_cast<ItemType>(subtype_id)) {
                case ItemType::Ammo:              // ammo
                case ItemType::Misc:              // charges - have strangely high values, or negative.
                    object->ammo = read_be_u32(); // bullets
                    break;
                case ItemType::Key:
                    object->keycode = read_be_u32(); // Observed as -1 in shipped maps.
                    break;
                case ItemType::Weapon:
                    object->ammo = read_be_u32();     // ammo
                    object->ammo_pid = read_be_u32(); // ammo pid
                    break;
                case ItemType::Armor:
                case ItemType::Container:
                case ItemType::Drug:
                    break;
                default:
                    throw ParseException("Unknown item type " + std::to_string(subtype_id) + " for proto "
                            + std::to_string(object->pro_pid),
                        _path, _stream.position());
            }
        } break;
        case ObjectType::Critter: {
            object->damage_last_turn = read_be_u32(); // engine CritterCombatData.damageLastTurn - saves only
            object->maneuver = read_be_u32();         // engine CritterCombatData.maneuver (CRITTER_MANEUVER_* bits) - saves only
            object->current_ap = read_be_u32();       // engine CritterCombatData.ap - saves only
            object->combat_results = read_be_u32();   // engine CritterCombatData.results (DAM_* bits) - saves only
            object->ai_packet = read_be_u32();        // AI packet - is it different from .pro? well, it can be
            object->group_id = read_be_u32();         // team - always 1? saves only?
            object->who_hit_me = read_be_u32();       // who hit me - saves only
            object->current_hp = read_be_u32();       // hit points - saves only, otherwise = value from .pro
            object->current_rad = read_be_u32();      // rad - always 0 - saves only
            object->current_poison = read_be_u32();   // poison - always 0 - saves only
        } break;

        case ObjectType::Scenery: {

            uint32_t subtype_id = pro->objectSubtypeId();
            switch (static_cast<SceneryType>(subtype_id)) {
                case SceneryType::LadderDown:
                case SceneryType::LadderUp:
                    object->map = read_be_u32();
                    object->elevhex = read_be_u32();
                    // hex = elevhex & 0xFFFF;
                    // elev = ((elevhex >> 28) & 0xf) >> 1;
                    break;
                case SceneryType::Stairs:
                    // looks like for ladders and stairs map and elev+hex fields in the different order
                    object->elevhex = read_be_u32();
                    object->map = read_be_u32();
                    // hex = elevhex & 0xFFFF;
                    // elev = ((elevhex >> 28) & 0xf) >> 1;
                    break;
                case SceneryType::Elevator:
                    object->elevtype = read_be_u32();  // elevator type - sometimes -1
                    object->elevlevel = read_be_u32(); // current level - sometimes -1
                    break;
                case SceneryType::Door:
                    object->walkthrough = read_be_u32(); // != 0 -> is opened;
                    break;
                case SceneryType::Generic:
                    break;
                default:
                    throw ParseException("Unknown scenery type " + std::to_string(subtype_id) + " for proto "
                            + std::to_string(object->pro_pid),
                        _path, _stream.position());
            }
        } break;
        case ObjectType::Wall:
        case ObjectType::Tile:
            break;
        case ObjectType::Misc:
            if (object->isExitGridMarker()) {
                object->exit_map = read_be_i32();
                object->exit_position = read_be_i32();
                object->exit_elevation = read_be_i32();
                object->exit_orientation = read_be_i32();
            }
            break;
        default:
            // The engine tolerates an object whose PID names no known type: objectDataRead
            // (fallout2-ce proto.cc) falls out of its type switch through `default: break`, so such a
            // record is the common block alone, with no type-specific tail — and objectDataWrite does
            // the same on the way out. RPU's epamain1/epamain2 each carry 17 records with pid -1, and
            // rejecting them made both maps unreadable. Keep the object, so a load/save round-trip
            // reproduces it byte for byte.
            spdlog::debug("Object with unrecognised type (pid {}) carries no type-specific data, as in the engine",
                static_cast<int32_t>(object->pro_pid));
            break;
    }

    // The inventory follows its holder, and every entry is a quantity plus a full object record that
    // may carry an inventory of its own (a bag in a critter's pack). The engine's _obj_load_obj
    // (fallout2-ce object.cc) recurses, so the reader must too: reading one level desynchronises the
    // stream at the first nested container.
    if (object->objects_in_inventory > 0) {
        object->inventory.reserve(object->objects_in_inventory);
        for (size_t i = 0; i < object->objects_in_inventory; ++i) {
            uint32_t amount = read_be_u32();
            std::unique_ptr<MapObject> subobject = readMapObject();
            subobject->amount = amount;
            object->inventory.push_back(std::move(subobject));
        }
    }

    return object;
}

std::unique_ptr<MapObject> MapReader::readObjectAt(const std::vector<uint8_t>& data, std::size_t offset, std::size_t& endOffset) {
    _stream = StreamBuffer(data);
    _path = "<object record>";
    _binaryUtils = std::make_unique<BinaryUtils>(_stream, _path);
    _stream.setPosition(offset);
    auto object = readMapObject();
    endOffset = _stream.position();
    return object;
}

// TODO: split
std::unique_ptr<Map> MapReader::read() {

    auto map = std::make_unique<Map>(_path);
    auto map_file = std::make_unique<Map::MapFile>();

    // 19 or 20
    auto version = read_be_u32();

    if (version == 19) {
        throw std::runtime_error{ "Fallout 1 maps are not supported yet" };
    }

    if (version != 20) {
        throw std::runtime_error{ "Unknown map version " + std::to_string(version) };
    }

    map_file->header.version = version;

    std::string filename = read_str(16);
    map_file->header.filename = filename;

    map_file->header.player_default_position = read_be_u32();
    map_file->header.player_default_elevation = read_be_u32();
    map_file->header.player_default_orientation = read_be_u32();

    uint32_t num_local_vars = read_be_u32();
    map_file->header.num_local_vars = num_local_vars;
    map_file->header.script_id = read_be_i32();

    uint32_t flags = read_be_u32();
    map_file->header.flags = flags;

    int elevations = 0;
    for (int elevation = 0; elevation < Map::ELEVATION_COUNT; ++elevation) {
        if (Map::elevationIsPresent(flags, elevation)) {
            elevations++;
        }
    }
    spdlog::debug("Map has {} elevation(s)", elevations);

    map_file->header.darkness = read_be_u32();

    uint32_t num_global_vars = read_be_u32();
    map_file->header.num_global_vars = num_global_vars;

    map_file->header.map_id = read_be_u32();
    map_file->header.timestamp = read_be_u32();

    skip<4 * 44>(); // skipping to the end of a MAP header

    for (uint32_t i = 0; i < num_global_vars; ++i) {
        map_file->map_global_vars.emplace_back(read_be_i32());
    }
    for (uint32_t i = 0; i < num_local_vars; ++i) {
        map_file->map_local_vars.emplace_back(read_be_i32());
    }

    for (int elevation = 0; elevation < Map::ELEVATION_COUNT; ++elevation) {
        if (!Map::elevationIsPresent(flags, elevation)) {
            continue;
        }
        spdlog::debug("Loading tiles at elevation {}", elevation);

        map_file->tiles[elevation].reserve(Map::TILES_PER_ELEVATION);

        for (auto tile_index = 0U; tile_index < Map::TILES_PER_ELEVATION; ++tile_index) {
            uint16_t roof = read_be_u16();
            uint16_t floor = read_be_u16();

            map_file->tiles[elevation].emplace_back(floor, roof);
        }
    }

    // SCRIPTS SECTION
    // Each section contains 16 slots for scripts
    for (unsigned script_section = 0; script_section < Map::SCRIPT_SECTIONS; script_section++) {
        uint32_t script_section_count = read_be_u32();
        map_file->scripts_in_section[script_section] = script_section_count;

        spdlog::debug("... script section {} has {} scripts", MapScript::toString(static_cast<MapScript::ScriptType>(script_section)), script_section_count);

        if (script_section_count > 0) {
            uint32_t loop = script_section_count;
            // find the next multiple of 16 higher than the count
            if (script_section_count % 16 > 0) {
                loop += 16 - script_section_count % 16;
            }

            uint32_t check = 0;
            for (unsigned j = 0; j < loop; j++) {

                MapScript map_script;

                int32_t pid = read_be_i32();

                map_script.pid = pid;
                map_script.next_script = read_be_u32(); // next script. unused

                switch (MapScript::fromPid(pid)) {
                    case MapScript::ScriptType::SYSTEM:
                        break;
                    case MapScript::ScriptType::SPATIAL:
                        map_script.timer = read_be_u32();
                        map_script.spatial_radius = read_be_u32();
                        break;
                    case MapScript::ScriptType::TIMER:
                        map_script.timer = read_be_u32();
                        break;
                    case MapScript::ScriptType::ITEM:
                    case MapScript::ScriptType::CRITTER:
                        break;
                    default:
                        // Padding slots (j >= script_section_count) hold leftover garbage the original
                        // mapper never zeroed; an unrecognised type byte there is expected and the slot is
                        // discarded below, so log it quietly. Only a *real* script (j < count) with an
                        // unknown type is worth a warning.
                        if (j < script_section_count) {
                            spdlog::warn("Unknown script PID type {} for a real script in section {}",
                                MapScript::sidSection(pid), script_section);
                        } else {
                            spdlog::debug("Skipping padding script slot (garbage PID type {}) in section {}",
                                MapScript::sidSection(pid), script_section);
                        }
                        break;
                }

                // Fixed trailer (layout shared with MapWriter via the visitor); each field is a
                // big-endian 32-bit word, interpreted per its own (signed/unsigned) type.
                visitMapScriptTrailerFields(map_script, [this]<class T>(T& field) {
                    field = static_cast<T>(read_be_u32());
                });

                if (j < script_section_count) {
                    map_file->map_scripts[script_section].push_back(map_script);
                }

                if ((j % 16) == 15) {

                    // TODO: write after the batch
                    // number of scripts in this batch (sequence)
                    uint32_t cur_check = read_be_u32();

                    spdlog::debug("Current script check for sequence {} is {}, j = {}", script_section, cur_check, j);

                    check += cur_check;

                    read_be_u32(); // uknown
                }
            }
            if (check != script_section_count) {
                throw std::runtime_error{ "Error reading scripts: check is incorrect" };
            }
        }
    }

    // OBJECTS SECTION
    // The engine (object.cc objectLoadAll / objectSaveAll) always frames this
    // section as exactly ELEVATION_COUNT per-elevation count blocks, regardless
    // of which elevations are enabled. Read all three unconditionally.
    read_be_u32(); // total object count across elevations — consumed; per-elevation counts follow

    for (int elev = 0; elev < Map::ELEVATION_COUNT; ++elev) {
        auto objectsOnElevation = read_be_u32();

        spdlog::debug("... loading {} map objects on elevation {}", objectsOnElevation, elev);
        for (size_t j = 0; j != objectsOnElevation; ++j) {

            std::unique_ptr<MapObject> object = readMapObject(); // with its inventory, recursively
            map_file->map_objects[elev].push_back(std::move(object));
        }

        if (objectsOnElevation != map_file->map_objects[elev].size()) {
            throw std::runtime_error{ "Object count doesn't match: " + std::to_string(objectsOnElevation) + " vs " + std::to_string(map_file->map_objects[elev].size()) };
        }
    }

    map->setMapFile(std::move(map_file));

    return map;
}

} // namespace geck
