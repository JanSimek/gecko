#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "format/map/MapScript.h"
#include "ssl/CallRefs.h"
#include "ssl/IntProgram.h"
#include "ssl/MessageRefs.h"
#include "ssl/ScriptFacts.h"
#include "ssl/VariableRefs.h"

namespace geck {
class Gam;
class Lst;
class Map;
class Msg;
}

namespace geck::resource {

class GameResources;

struct ScriptLocation {
    std::string file; ///< generic path as the provider named it; empty when unknown
    uint32_t line = 0;
};

/// A problem in a script that the compiler does not catch.
struct ScriptFinding {
    enum class Severity {
        Error,
        Warning,
        Note,
    };
    Severity severity = Severity::Error;
    std::string check; ///< e.g. "syntax", "local-vars", "messages", "gvars", "timers", "objects", "tiles"
    std::string message;
    ScriptLocation where;
    std::string procedure;
};

/// "error", "warning" or "note".
const char* severityName(ScriptFinding::Severity severity);

struct ScriptProcedure {
    std::string name;
    bool live = false;       ///< compiled into the script (see ssl::CallGraph::isLive)
    bool entryPoint = false; ///< a procedure the engine calls by name
    std::size_t parameters = 0;
    ScriptLocation where;
    bool inScript = false; ///< written in the script itself, not a header or template it includes
};

template <typename Ref>
struct ScriptUse {
    Ref ref;
    ScriptLocation where;
};

/// Everything the lint, the analysis and the editor need from one parsed script, with locations as
/// paths so the report outlives the parse.
struct ScriptReport {
    std::string script; ///< the script file's path
    /// Read from the compiled .int, without a source: see reportInt for what that leaves out.
    bool fromBytecode = false;
    std::vector<ScriptFinding> syntax;
    std::vector<ScriptProcedure> procedures;
    std::vector<ScriptUse<ssl::VariableRef>> globals;
    std::vector<ScriptUse<ssl::VariableRef>> locals;
    std::vector<ScriptUse<ssl::MessageRef>> messages;
    /// add_timer_event, create_object_sid, move_to and critter_attempt_placement calls.
    std::vector<ScriptUse<ssl::CallRef>> calls;
    std::vector<ssl::ScriptFacts::Override> overrides;
    std::map<int, std::string> localVarNames;
};

ScriptReport reportScript(const ssl::ScriptUnit& unit);

/// What the compiled script at `path` shows when no source is mounted: its procedures, and its local
/// variable, global variable, message and ScriptReport::calls uses. Each opcode's arguments are found by
/// walking back over the code with the engine's stack effects (IntOpcodes.inc); an argument is known
/// when its code folds to a constant. A branch under a constant condition is left out, as sslc drops it
/// (the original compiler kept it in the .int). An argument behind control flow stays unknown; a write
/// with an unknown index counts as computed. Locations are the .int with no line, and macros, includes
/// and overrides are not known.
ScriptReport reportInt(const ssl::IntProgram& program, std::string path);

/// reportInt for the mounted scripts/<baseName>.int, or nullopt when it is absent or unreadable.
std::optional<ScriptReport> reportCompiled(GameResources& resources, const std::string& baseName);

/// Parse each script at `paths` (VFS paths) through the mounted data, on all cores.
std::vector<ScriptReport> reportScripts(GameResources& resources, const std::vector<std::filesystem::path>& paths);

/// Turns a report into findings, with what the checks need from the game data: scripts.lst's local
/// variable counts and script numbers, the dialog .msg files messages are read from, vault13.gam's
/// global variables and the protos. Checks whose data is not mounted are skipped. Not thread-safe.
class ScriptLinter {
public:
    /// `scripts` may be null (no game data): the checks that need it are skipped.
    ScriptLinter(GameResources& resources, const Lst* scripts, std::string locale);

    /// The findings for the script at 0-based `programIndex` (-1 when it is not in scripts.lst):
    ///  - "syntax": what the preprocessor or parser could not read;
    ///  - "local-vars" (error): a local variable number past the count scripts.lst gives the script
    ///    (`# local_vars=N`). The engine checks only the map's whole array (fallout2-ce map.cc
    ///    mapGetLocalVar), so it silently reads and writes the next script's variables;
    ///  - "messages" (error): a message number its dialog .msg does not have (the engine shows
    ///    "Error"), or a list that names no script; a warning when the .msg itself is missing;
    ///  - "gvars" (error): a global variable number past vault13.gam's (game.cc gameGetGlobalVar
    ///    refuses it: reads give 0, writes are dropped);
    ///  - "timers" (warning): add_timer_event on self_obj without a timed_event_p_proc, which the
    ///    event runs (scripts.cc), so it does nothing;
    ///  - "objects" (error): create_object_sid with PID 0 or a PID that has no proto (nothing is
    ///    created), or a script number that is not a scripts.lst line (the engine stores it minus 1);
    ///  - "tiles" (error): create_object_sid, move_to or critter_attempt_placement on a constant hex
    ///    outside 0-39999 or an elevation outside 0-2, which objectSetLocation refuses;
    ///  - notes, when `includeNotes`: "unused-procedure", a procedure written in the script that
    ///    nothing calls, which sslc leaves out of the compiled script; "local-vars", a local variable
    ///    the script reads but never sets (always 0) or sets but never reads.
    std::vector<ScriptFinding> findings(const ScriptReport& report, int programIndex, bool includeNotes);

    /// The `# local_vars=N` scripts.lst gives the script at `programIndex`; 0 when it gives none.
    int localVariableCount(int programIndex) const;

private:
    void localVariables(const ScriptReport& report, int programIndex, std::vector<ScriptFinding>& out) const;
    void messages(const ScriptReport& report, std::vector<ScriptFinding>& out);
    void globalVariables(const ScriptReport& report, std::vector<ScriptFinding>& out);
    void timers(const ScriptReport& report, std::vector<ScriptFinding>& out) const;
    void objectsAndTiles(const ScriptReport& report, std::vector<ScriptFinding>& out);
    void createdObject(const ScriptUse<ssl::CallRef>& use, std::vector<ScriptFinding>& out);
    void unusedLocalVariables(const ScriptReport& report, std::vector<ScriptFinding>& out) const;
    const Msg* dialogOf(const std::string& name);
    bool protoExists(int64_t pid);

    GameResources& _resources;
    const Lst* _scripts;
    std::string _locale;
    std::map<std::string, const Msg*, std::less<>> _dialogs;
    std::map<int64_t, bool> _protos;
    Gam* _gam = nullptr;
    bool _gamLoaded = false;
};

/// Where a map places a script: the map's own (header) script, or one of the map_scripts sections.
enum class ScriptPlacement {
    Map, ///< the map's own script, or one in the System section: the engine treats both as system scripts
    Spatial,
    Timer,
    Item,
    Critter,
};

/// The placement of a map_scripts entry of `type`.
ScriptPlacement placementOf(MapScript::ScriptType type);

/// What is wrong with a script for where a map places it:
///  - "local-vars" (error): the map's own script uses local variables; the engine refuses a system
///    script's local_var (scripts.cc scriptGetLocalVar), so every read gives -1;
///  - "spatial" (warning): a spatial script without spatial_p_proc, which its trigger runs, so
///    stepping into it does nothing;
///  - "handlers" (note): critter_p_proc or combat_p_proc in a script that is not a critter's, which
///    the engine only calls for the critter script list (scripts.cc, combat.cc).
std::vector<ScriptFinding> placementFindings(const ScriptReport& report, ScriptPlacement placement);

/// Global variables that scripts read but no script sets, across `reports` (every script the game
/// runs, so the answer is only as complete as the mounted sources). Such a gvar keeps its vault13.gam
/// start value, so whatever waits for it to change never happens. One "gvars" warning per variable,
/// at its first read, keyed by the index of the report it is in. Writes with a computed variable
/// number could set any of them; `dynamicWrites` counts them so the caller can say so. Reports read
/// from a compiled script (fromBytecode) take part like the others: see reportInt.
struct NeverSetGlobals {
    std::map<std::size_t, std::vector<ScriptFinding>> findings;
    std::size_t dynamicWrites = 0;
};
NeverSetGlobals neverSetGlobals(const std::vector<ScriptReport>& reports, const Gam& gam);

/// The global variables the engine itself writes (fallout2-ce): GVAR_LOAD_MAP_INDEX (load_map),
/// GVAR_TOWN_REP_ARROYO, and the drug addiction flags (item.cc). A gvar no script sets but one of
/// these still changes.
bool engineWritesGlobal(std::string_view gvarName);

/// What is wrong with where a map puts each placed script's local variables, by script SID, as a
/// sentence ("Its local variables 0-19 run past the map's 2, ...").
///
/// The engine gives a script the block of the map's local variable array that starts at its record's
/// offset. Its length is scripts.lst's `# local_vars=N`, except in a saved game, where the record's own
/// count is used when it is not 0 (fallout2-ce scripts.cc scriptRead zeroes it for a map that is not
/// saved, and scriptGetLocalVar then reads scripts.lst). Nothing checks one block against another
/// (map.cc mapGetLocalVar only bounds the whole array), so a block that overlaps another script's
/// makes the two share variables, and one past the end of the array fails to read and write.
std::map<uint32_t, std::string> localVarBlockProblems(const Map& map, const Lst& scripts);

} // namespace geck::resource
