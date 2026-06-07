#pragma once

#include <inttypes.h>
#include <string_view>

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
        unsigned int pid = (val & 0xff000000) >> 24;

        switch (pid) {
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
};

} // namespace geck
