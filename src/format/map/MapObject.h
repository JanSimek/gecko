#pragma once

#include <inttypes.h>
#include <memory>
#include <vector>
#include <string>
#include "../../util/Constants.h"

namespace geck {

class Pro; // Forward declaration

// scenery, walls, items, containers, keys and critters
struct MapObject {

    uint32_t unknown0; // falltergeist OID ?
    int32_t position;  // hex position
    uint32_t x;
    uint32_t y;
    int32_t sx;
    int32_t sy;
    uint32_t frame_number;
    uint32_t direction;
    uint32_t frm_pid; // FID
    uint32_t flags;
    uint32_t elevation;
    uint32_t pro_pid;      // PID
    int32_t critter_index; // combat id
    uint32_t light_radius;
    uint32_t light_intensity;
    uint32_t outline_color;
    int32_t map_scripts_pid; // SID
    int32_t script_id;

    // Inventory
    uint32_t objects_in_inventory;
    uint32_t max_inventory_size;
    std::vector<std::unique_ptr<MapObject>> inventory;
    uint32_t amount;

    uint32_t unknown10; // unknown12 ?
    uint32_t unknown11; // unknown13 ?

    // Extra fields for critters
    uint32_t player_reaction; // reaction to player - saves only
    uint32_t current_mp;      // current mp - saves only
    uint32_t combat_results;  // combat results - saves only
    uint32_t dmg_last_turn;   // damage last turn - saves only
    uint32_t ai_packet;       // AI packet - is it different from .pro? well, it can be
    uint32_t group_id;        // team - always 1? saves only?
    uint32_t who_hit_me;      // who hit me - saves only
    uint32_t current_hp;      // hit points - saves only, otherwise = value from .pro
    uint32_t current_rad;     // rad - always 0 - saves only
    uint32_t current_poison;

    // Extra fields for ammo
    // Extra fields for misc items
    uint32_t ammo; // charges for misc items

    // Extra fields for keys
    uint32_t keycode;

    // Extra fields for weapons
    // + ammo above
    uint32_t ammo_pid;

    // Extra fields for ladders
    // Extra fields for stairs
    uint32_t elevhex; // elevation + hex
    uint32_t map;

    // Extra fields for portals/doors
    uint32_t walkthrough;

    // Extra fields for elevators
    uint32_t elevtype;
    uint32_t elevlevel;

    // Extra fields for exit grids
    uint32_t exit_map;
    uint32_t exit_position;
    uint32_t exit_elevation;
    uint32_t exit_orientation;

    bool isBlocker() {
        auto baseId = frm_pid & 0x00FFFFFF;
        return baseId == 1 && flags & 0x00000010;
    }

    bool isWallBlocker() {
        // Any object that blocks movement OR is a gap-filling wall blocker
        return blocksMovement() || isGapFillingWallBlocker();
    }

    bool isNormalWallBlocker() {
        return pro_pid == (0x05000000 | 620);
    }

    bool isShootThroughWallBlocker() {
        return pro_pid == (0x05000000 | 621);
    }

    bool isScrollBlocker() {
        // Scroll blockers are visual indicators only (FRM-based, not proto-based)
        // They use scrblk.frm (FRM baseId == 1)
        auto baseId = frm_pid & 0x00FFFFFF;
        return baseId == 1;
    }

private:
    // Helper method to get PRO file data on demand
    Pro* getProData() const;

public:
    // Check if object blocks movement based on PRO file flags
    bool blocksMovement() const;
    
    // Check if object is a gap-filling wall blocker (PIDs 620/621)
    bool isGapFillingWallBlocker() const;
    
    // Check if object is a wall (OBJECT_TYPE::WALL)
    bool isWallObject() const;
};

} // namespace geck
