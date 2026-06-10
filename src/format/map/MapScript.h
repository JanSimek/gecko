#pragma once

#include <inttypes.h>
#include <string_view>

#include "util/BuiltTile.h"

namespace geck {

struct MapScript {
    uint32_t pid;
    uint32_t next_script;    // unused
    uint32_t timer;          // only if PID = 1 or 2
    uint32_t spatial_radius; // only if PID = 1
    uint32_t flags;
    uint32_t script_id;
    uint32_t unknown5;
    uint32_t script_oid;
    uint32_t local_var_offset;
    uint32_t local_var_count;
    uint32_t unknown9;
    uint32_t unknown10;
    uint32_t unknown11;
    uint32_t unknown12;
    uint32_t unknown13;
    uint32_t unknown14;
    uint32_t unknown15;
    uint32_t unknown16;

    enum class ScriptType : uint32_t {
        SYSTEM = 0,
        SPATIAL,
        TIMER,
        ITEM,
        CRITTER,
        UNKNOWN
    };

    static ScriptType fromPid(uint32_t val) {
        switch (sidSection(val)) {
            case 0:
                return ScriptType::SYSTEM;
            case 1:
                return ScriptType::SPATIAL;
            case 2:
                return ScriptType::TIMER;
            case 3:
                return ScriptType::ITEM;
            case 4:
                return ScriptType::CRITTER;
            default:
                return ScriptType::UNKNOWN;
        }
    }

    static std::string_view toString(ScriptType type) {
        switch (type) {
            case ScriptType::SYSTEM:
                return "System";
            case ScriptType::SPATIAL:
                return "Spatial";
            case ScriptType::TIMER:
                return "Timer";
            case ScriptType::ITEM:
                return "Item";
            case ScriptType::CRITTER:
                return "Critter";
            default:
                return "Unknown";
        }
    }

    // A SID/PID packs the script type in the high byte and the per-type index in
    // the low 24 bits. The map_scripts section index equals the script type.
    static constexpr unsigned SID_TYPE_SHIFT = 24;
    static constexpr uint32_t SID_INDEX_MASK = 0x00FFFFFF;
    static constexpr uint32_t NONE = 0xFFFFFFFFu; // engine -1 sentinel

    static constexpr uint32_t makeSid(ScriptType type, uint32_t index) {
        return (static_cast<uint32_t>(type) << SID_TYPE_SHIFT) | (index & SID_INDEX_MASK);
    }
    static constexpr int sidSection(uint32_t sid) {
        return static_cast<int>((sid >> SID_TYPE_SHIFT) & 0xFFu);
    }
    static constexpr uint32_t sidIndex(uint32_t sid) {
        return sid & SID_INDEX_MASK;
    }

    /// Object-instance script with the engine's scriptAdd defaults (local vars
    /// allocated at runtime; actionBeingUsed = -1). The owner object references
    /// this script through its SID (== this script's pid).
    static MapScript makeObjectScript(ScriptType type, uint32_t scriptId,
        uint32_t programIndex, uint32_t ownerOid) {
        MapScript s {};
        s.pid = makeSid(type, scriptId);
        s.script_id = programIndex;
        s.script_oid = ownerOid;
        s.local_var_offset = NONE;
        s.local_var_count = 0;
        s.unknown12 = NONE; // actionBeingUsed
        return s;
    }

    /// Spatial (hex trigger-zone) script placed at a tile with a radius.
    static MapScript makeSpatialScript(uint32_t scriptId, uint32_t programIndex,
        uint32_t tile, uint32_t elevation, uint32_t radius, uint32_t ownerOid) {
        MapScript s = makeObjectScript(ScriptType::SPATIAL, scriptId, programIndex, ownerOid);
        s.timer = built_tile::create(tile, elevation); // spatial: built_tile
        s.spatial_radius = radius;
        return s;
    }
};

} // namespace geck
