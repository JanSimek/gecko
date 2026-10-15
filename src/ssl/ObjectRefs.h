#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ssl/CallGraph.h"
#include "ssl/ScriptUnit.h"

namespace geck::ssl {

/// A constant PID the script uses: passed where an opcode takes one, or compared with obj_pid().
struct PidRef {
    int64_t pid = 0;
    std::string via; ///< the opcode that takes it ("create_object_sid", ...), or "obj_pid" for a comparison
    std::string procedure;
    SourceLocation location; ///< where the user wrote it
};

/// Every constant PID in the live procedures of `unit`, in source order: the PID argument of
/// create_object_sid, obj_is_carrying_obj_pid, obj_carrying_pid_obj, tile_contains_obj_pid,
/// tile_contains_pid_obj and party_member_obj (fallout2-ce interpreter_extra.cc pops it as a ProtoId),
/// and either side of `obj_pid(...) == PID` / `!=`.
std::vector<PidRef> pidRefs(const ScriptUnit& unit, const CallGraph& graph);

/// A load_map call: the map it sends the player to and the entrance it passes.
struct MapRef {
    std::optional<int64_t> index;    ///< the maps.txt index, when the map argument folds to a number
    std::optional<std::string> name; ///< the map's file name, when the argument is a string
    std::optional<int64_t> entrance; ///< the second argument, which the engine stores in GVAR_LOAD_MAP_INDEX
    std::string procedure;
    SourceLocation location; ///< where the user wrote it
};

/// Every load_map call in the live procedures of `unit`, in source order. The engine takes the map as
/// a maps.txt index or a file name (fallout2-ce interpreter_extra.cc opLoadMap, wmMapMatchNameToIdx).
std::vector<MapRef> mapRefs(const ScriptUnit& unit, const CallGraph& graph);

} // namespace geck::ssl
