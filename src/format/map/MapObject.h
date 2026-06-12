#pragma once

#include <inttypes.h>
#include <memory>
#include <vector>
#include <string>
#include "util/Constants.h"
#include <spdlog/spdlog.h>

namespace geck {

/// A single map object: scenery, walls, items, containers, keys or critters.
struct MapObject {

    uint32_t unknown0 = 0; // falltergeist OID ?
    int32_t position = 0;  // hex position
    uint32_t x = 0;
    uint32_t y = 0;
    int32_t sx = 0;
    int32_t sy = 0;
    uint32_t frame_number = 0;
    uint32_t direction = 0;
    uint32_t frm_pid = 0; // FID
    uint32_t flags = 0;
    uint32_t elevation = 0;
    uint32_t pro_pid = 0;       // PID
    int32_t critter_index = -1; // combat id (-1 == none)
    uint32_t light_radius = 0;
    uint32_t light_intensity = 0;
    uint32_t outline_color = 0;
    int32_t map_scripts_pid = -1; // SID (-1 == none)
    int32_t script_id = -1;       // -1 == none

    // Inventory
    uint32_t objects_in_inventory = 0;
    uint32_t max_inventory_size = 0;
    std::vector<std::unique_ptr<MapObject>> inventory;
    uint32_t amount = 1;

    uint32_t unknown10 = 0; // unknown12 ?
    uint32_t unknown11 = 0; // unknown13 ?

    // Extra fields for critters
    uint32_t player_reaction = 0; // reaction to player - saves only
    uint32_t current_mp = 0;      // current mp - saves only
    uint32_t combat_results = 0;  // combat results - saves only
    uint32_t dmg_last_turn = 0;   // damage last turn - saves only
    uint32_t ai_packet = 0;       // AI packet - is it different from .pro? well, it can be
    uint32_t group_id = 0;        // team - always 1? saves only?
    uint32_t who_hit_me = 0;      // who hit me - saves only
    uint32_t current_hp = 0;      // hit points - saves only, otherwise = value from .pro
    uint32_t current_rad = 0;     // rad - always 0 - saves only
    uint32_t current_poison = 0;

    // Extra fields for ammo
    // Extra fields for misc items
    uint32_t ammo = 0; // charges for misc items

    // Extra fields for keys
    uint32_t keycode = 0;

    // Extra fields for weapons
    // + ammo above
    uint32_t ammo_pid = 0;

    // Extra fields for ladders
    // Extra fields for stairs
    uint32_t elevhex = 0; // elevation + hex
    uint32_t map = 0;

    // Extra fields for portals/doors
    uint32_t walkthrough = 0;

    // Extra fields for elevators
    uint32_t elevtype = 0;
    uint32_t elevlevel = 0;

    // Extra fields for exit grids
    uint32_t exit_map = 0;
    uint32_t exit_position = 0;
    uint32_t exit_elevation = 0;
    uint32_t exit_orientation = 0;

    /// Object type from the PID's high byte (matches engine PID_TYPE). Values are
    /// Pro::OBJECT_TYPE — ITEM=0, CRITTER=1, SCENERY=2, WALL=3, TILE=4, MISC=5.
    uint32_t objectType() const { return (pro_pid & FileFormat::FULL_TYPE_MASK) >> FileFormat::TYPE_MASK_SHIFT; }

    /// Proto index: the PID's low 24 bits.
    uint32_t protoId() const { return pro_pid & FileFormat::BASE_ID_MASK; }

    /// Art index: the FID's (frm_pid) low 24 bits.
    uint32_t fidBaseId() const { return frm_pid & FileFormat::BASE_ID_MASK; }

    bool isBlocker() {
        return fidBaseId() == 1 && flags & 0x00000010;
    }

    bool isScrollBlocker() {
        // Scroll blockers are visual indicators only (FRM-based, not proto-based)
        // They use scrblk.frm (FRM baseId == 1)
        return fidBaseId() == 1;
    }

    bool isWallObject() const;

    /// Deep copy of this object, recursively cloning inventory. Needed because
    /// MapObject is otherwise non-copyable (the inventory holds unique_ptrs).
    std::unique_ptr<MapObject> cloneDeep() const;

    bool isLightSource() const {
        bool hasLight = light_radius > 0 || light_intensity > 0;
        return hasLight;
    }

    bool hasSignificantLight() const {
        return light_radius > 0 && light_intensity > 0;
    }

    /// True for the specific light-source scenery object: ITEM/index-0 type with PID index 140 (tile #140 in F2 Dims).
    bool isLightSourceScenery() const {
        return objectType() == 0 && protoId() == 140;
    }

    /// True for exit-grid markers: MISC objects (type 0x05) with PID index 16-23 (matches legacy F2 Mapper: misc_ID && nID 16..23).
    bool isExitGridMarker() const {
        return (objectType() == 0x05) && (protoId() >= 16) && (protoId() <= 23);
    }
};

} // namespace geck
