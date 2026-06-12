#include "MapObject.h"
#include "format/pro/Pro.h"

namespace geck {

bool MapObject::isWallObject() const {
    return objectType() == static_cast<uint32_t>(Pro::OBJECT_TYPE::WALL);
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
    c->player_reaction = player_reaction;
    c->current_mp = current_mp;
    c->combat_results = combat_results;
    c->dmg_last_turn = dmg_last_turn;
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

} // namespace geck
