#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "ssl/MessageRefs.h"
#include "ssl/ScriptFacts.h"
#include "ssl/VariableRefs.h"

namespace geck {
class Lst;
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
    std::string check; ///< "syntax", "local-vars", "messages" or "unused-procedure"
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
    std::vector<ScriptFinding> syntax;
    std::vector<ScriptProcedure> procedures;
    std::vector<ScriptUse<ssl::VariableRef>> globals;
    std::vector<ScriptUse<ssl::VariableRef>> locals;
    std::vector<ScriptUse<ssl::MessageRef>> messages;
    std::vector<ssl::ScriptFacts::Override> overrides;
    std::map<int, std::string> localVarNames;
};

ScriptReport reportScript(const ssl::ScriptUnit& unit);

/// Parse each script at `paths` (VFS paths) through the mounted data, on all cores.
std::vector<ScriptReport> reportScripts(GameResources& resources, const std::vector<std::filesystem::path>& paths);

/// Turns a report into findings, with what the checks need from the game data: scripts.lst's local
/// variable counts, and the dialog .msg files messages are read from. Not thread-safe.
class ScriptLinter {
public:
    ScriptLinter(GameResources& resources, const Lst& scripts, std::string locale);

    /// The findings for the script at 0-based `programIndex`:
    ///  - "syntax": what the preprocessor or parser could not read;
    ///  - "local-vars" (error): a local variable number past the count scripts.lst gives the script
    ///    (`# local_vars=N`). The engine checks only the map's whole array (fallout2-ce map.cc
    ///    mapGetLocalVar), so it silently reads and writes the next script's variables;
    ///  - "messages" (error): a message number its dialog .msg does not have (the engine shows
    ///    "Error"), or a list that names no script; a warning when the .msg itself is missing;
    ///  - "unused-procedure" (note, when `includeNotes`): a procedure written in the script that
    ///    nothing calls, which sslc leaves out of the compiled script.
    std::vector<ScriptFinding> findings(const ScriptReport& report, int programIndex, bool includeNotes);

    /// The `# local_vars=N` scripts.lst gives the script at `programIndex`; 0 when it gives none.
    int localVariableCount(int programIndex) const;

private:
    void localVariables(const ScriptReport& report, int programIndex, std::vector<ScriptFinding>& out) const;
    void messages(const ScriptReport& report, std::vector<ScriptFinding>& out);
    const Msg* dialogOf(const std::string& name);

    GameResources& _resources;
    const Lst& _scripts;
    std::string _locale;
    std::map<std::string, const Msg*, std::less<>> _dialogs;
};

} // namespace geck::resource
