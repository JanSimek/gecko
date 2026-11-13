#include "MapObject.h"
#include "format/pro/Pro.h"
#include "util/Constants.h"

#include <algorithm>
#include <array>

namespace geck {

namespace {

    using Flag = Pro::ObjectFlags;

    // Proto flags obj_new copies onto the object as they are; the proto and object words share bits.
    constexpr std::array COPIED_FLAGS = { Flag::OBJECT_FLAT, Flag::OBJECT_NO_BLOCK, Flag::OBJECT_MULTIHEX,
        Flag::OBJECT_LIGHT_THRU, Flag::OBJECT_SHOOT_THRU, Flag::OBJECT_WALL_TRANS_END, Flag::OBJECT_NO_HIGHLIGHT };

    // An object takes one transparency, the first of these the proto sets (obj_new's if/else chain).
    constexpr std::array TRANSPARENCY_FLAGS = { Flag::OBJECT_TRANS_NONE, Flag::OBJECT_TRANS_WALL,
        Flag::OBJECT_TRANS_GLASS, Flag::OBJECT_TRANS_STEAM, Flag::OBJECT_TRANS_ENERGY, Flag::OBJECT_TRANS_RED };

    // objectSetLight clamps the radius to 8 hexes and drops a light with no intensity; a lit object
    // also gets OBJECT_LIGHTING (_obj_turn_on_light), without which the engine never lights its hexes.
    constexpr uint32_t MAX_LIGHT_RADIUS = 8;

    void seedFlagsAndLight(MapObject& object, const Pro& pro) {
        const uint32_t protoFlags = pro.header.flags;
        object.flags = 0;
        for (const Flag flag : COPIED_FLAGS) {
            if (Pro::hasFlag(protoFlags, flag)) {
                object.flags = Pro::setFlag(object.flags, flag);
            }
        }
        if (const auto transparency = std::ranges::find_if(TRANSPARENCY_FLAGS,
                [protoFlags](Flag flag) { return Pro::hasFlag(protoFlags, flag); });
            transparency != TRANSPARENCY_FLAGS.end()) {
            object.flags = Pro::setFlag(object.flags, *transparency);
        }

        if (static_cast<int32_t>(pro.header.light_intensity) > 0) {
            object.light_radius = std::min(pro.header.light_distance, MAX_LIGHT_RADIUS);
            object.light_intensity = pro.header.light_intensity;
            object.flags = Pro::setFlag(object.flags, Flag::OBJECT_LIGHTING);
        } else {
            object.light_radius = 0;
            object.light_intensity = 0;
        }
    }

    // _proto_update_gen: a new item starts loaded/charged and keyed as its proto says.
    void seedItemData(MapObject& object, const Pro& pro) {
        using enum ItemType;
        switch (pro.itemType()) {
            case Weapon:
                object.ammo = pro.weaponData.ammoCapacity;
                object.ammo_pid = static_cast<uint32_t>(pro.weaponData.ammoPID);
                break;
            case Ammo:
                object.ammo = pro.ammoData.quantity;
                break;
            case Misc:
                object.ammo = pro.miscData.charges;
                break;
            case Key:
                object.keycode = pro.keyData.keyId;
                break;
            default:
                break;
        }
    }

    // _proto_update_gen: doors, stairs, elevators and ladders take their proto's state and target.
    void seedSceneryData(MapObject& object, const Pro& pro) {
        const auto& scenery = pro.sceneryData;
        using enum SceneryType;
        switch (static_cast<SceneryType>(pro.objectSubtypeId())) {
            case Door:
                object.walkthrough = scenery.doorData.walkThroughFlag;
                break;
            case Stairs:
                object.elevhex = scenery.stairsData.destTile;
                object.map = scenery.stairsData.destElevation; // the engine's destinationMap
                break;
            case Elevator:
                object.elevtype = scenery.elevatorData.elevatorType;
                object.elevlevel = scenery.elevatorData.elevatorLevel;
                break;
            case LadderUp:
            case LadderDown:
                object.elevhex = scenery.ladderData.destTileAndElevation;
                object.map = 0; // a ladder proto carries no map
                break;
            default:
                break;
        }
    }

    // _proto_update_init: a new critter is unhurt, at full action points, with its proto's AI packet
    // and team, and nobody has hit it. critterGetStat clamps HP to 0..999 and AP to 1..99.
    void seedCritterData(MapObject& object, const Pro& pro) {
        const auto& critter = pro.critterData;
        const auto stat = [](uint32_t base, uint32_t bonus, int64_t lo, int64_t hi) {
            return static_cast<uint32_t>(std::clamp(static_cast<int64_t>(static_cast<int32_t>(base))
                    + static_cast<int32_t>(bonus),
                lo, hi));
        };
        object.current_hp = stat(critter.maxHitPoints, critter.bonusHealthPoints, 0, 999);
        object.current_ap = stat(critter.actionPoints, critter.bonusActionPoints, 1, 99);
        object.ai_packet = critter.aiPacket;
        object.group_id = critter.teamNumber;
        object.who_hit_me = 0xFFFFFFFF; // whoHitMeCid -1: nobody
        object.damage_last_turn = 0;
        object.maneuver = 0;
        object.combat_results = 0;
        object.current_rad = 0;
        object.current_poison = 0;
    }

} // namespace

bool MapObject::isWallObject() const {
    return objectType() == ObjectType::Wall;
}

std::unique_ptr<MapObject> MapObject::cloneDeep() const {
    auto c = std::make_unique<MapObject>();
    c->unknown0 = unknown0;
    c->position = position;
    c->x = x;
    c->y = y;
    c->sx = sx;
    c->sy = sy;
    c->frame_number = frame_number;
    c->direction = direction;
    c->frm_pid = frm_pid;
    c->flags = flags;
    c->elevation = elevation;
    c->pro_pid = pro_pid;
    c->critter_index = critter_index;
    c->light_radius = light_radius;
    c->light_intensity = light_intensity;
    c->outline_color = outline_color;
    c->map_scripts_pid = map_scripts_pid;
    c->script_id = script_id;
    c->objects_in_inventory = objects_in_inventory;
    c->max_inventory_size = max_inventory_size;
    c->amount = amount;
    c->unknown10 = unknown10;
    c->unknown11 = unknown11;
    c->damage_last_turn = damage_last_turn;
    c->maneuver = maneuver;
    c->current_ap = current_ap;
    c->combat_results = combat_results;
    c->ai_packet = ai_packet;
    c->group_id = group_id;
    c->who_hit_me = who_hit_me;
    c->current_hp = current_hp;
    c->current_rad = current_rad;
    c->current_poison = current_poison;
    c->ammo = ammo;
    c->keycode = keycode;
    c->ammo_pid = ammo_pid;
    c->elevhex = elevhex;
    c->map = map;
    c->walkthrough = walkthrough;
    c->elevtype = elevtype;
    c->elevlevel = elevlevel;
    c->exit_map = exit_map;
    c->exit_position = exit_position;
    c->exit_elevation = exit_elevation;
    c->exit_orientation = exit_orientation;
    c->inventory.reserve(inventory.size());
    for (const auto& item : inventory) {
        if (item) {
            c->inventory.push_back(item->cloneDeep());
        }
    }
    return c;
}

void MapObject::applyProtoDefaults(const Pro& pro) {
    seedFlagsAndLight(*this, pro);
    using enum ObjectType;
    switch (pro.type()) {
        case Item:
            seedItemData(*this, pro);
            break;
        case Scenery:
            seedSceneryData(*this, pro);
            break;
        case Critter:
            seedCritterData(*this, pro);
            break;
        case Misc:
            if (const auto id = pro.pid().protoId(); id >= static_cast<uint32_t>(MiscProtoTypeId::FirstExitGrid)
                && id <= static_cast<uint32_t>(MiscProtoTypeId::LastExitGrid)) {
                // _proto_update_gen: an exit grid leads nowhere until it is given a destination.
                exit_map = ExitGrid::TOWN_MAP_EXIT; // MAP_INVALID, -1
                exit_position = 0xFFFFFFFF;         // tile -1
                exit_elevation = 0;
                exit_orientation = 0; // ROTATION_NE
            }
            break;
        default:
            break;
    }
}

} // namespace geck
