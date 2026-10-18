#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

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

/// The scripts whose live code calls `name(...)` (an opcode or procedure, as script_refs kind "call"),
/// as lowercase stems such as "dcmerch" — the form a scripts.lst entry takes once its extension is
/// dropped. `scriptsRead` (optional) gets how many sources were read, 0 meaning no source tree is mounted.
std::set<std::string, std::less<>> scriptsCalling(resource::GameResources& resources, const std::string& name,
    std::size_t* scriptsRead = nullptr);

/// One load_map call in code a compiled script runs: a script-driven map transition.
struct ScriptMapTransition {
    std::string script; ///< the .ssl file name, e.g. "hcoutfit.ssl"
    std::string file;   ///< the header or template it is written in, when not the script itself
    uint32_t line = 0;
    std::string procedure;
    std::string text;                ///< the source line
    std::optional<int> mapIndex;     ///< when the script names the map by maps.txt index
    std::string mapFile;             ///< the target's .map file name, "" when maps.txt doesn't resolve it
    std::optional<int64_t> entrance; ///< the value the engine stores in GVAR_LOAD_MAP_INDEX
};

/// Every load_map call in the mounted .ssl sources (see script_refs kind "map"), in script order;
/// `scriptsRead` (optional) gets how many sources were read, 0 meaning no source tree is mounted.
std::vector<ScriptMapTransition> scriptMapTransitions(resource::GameResources& resources, std::size_t* scriptsRead = nullptr);

} // namespace geck::cli
