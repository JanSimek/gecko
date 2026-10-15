#pragma once

#include <iosfwd>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

struct ScriptRefsOptions {
    /// What to look for:
    ///  - "gvar":      reads and writes of a global variable (GVAR_* name)
    ///  - "call":      calls `name(...)` of an opcode or procedure, e.g. load_map, create_object_sid
    ///  - "procedure": every reference to a procedure: calls, `call name`, names passed as values, @name
    ///  - "macro":     code a macro produced, e.g. set_self_ai or a quest-flag alias
    ///  - "pid":       a constant PID (a number, decimal or 0x hex, or a PID_* macro) where an opcode
    ///                 takes one (create_object_sid, obj_is_carrying_obj_pid, party_member_obj, ...)
    ///                 or compared with obj_pid(); "*" lists every one
    ///  - "map":       load_map calls sending the player to a map (file name, any case, ".map"
    ///                 optional, or maps.txt index), whichever way the script names it; "*" lists all
    std::string kind;
    std::string name;
};

/// Search every mounted .ssl script (preprocessed and parsed, see ssl/) for `options.name` and report
/// each use in code a compiled script runs, as {script, file?, line, procedure, text} plus per-kind
/// fields (gvar: kind get/set, via; pid: via, pid, name; map: map, mapIndex, entrance). Uses in comments, unused macros, procedures nothing calls and
/// branches on a constant are not reported. Emits a JSON object to `out`; returns 0 on success
/// (including "not used"), nonzero for a bad kind or name or when no .ssl source is mounted.
int findScriptRefs(resource::GameResources& resources, const ScriptRefsOptions& options, std::ostream& out);

} // namespace geck::cli
