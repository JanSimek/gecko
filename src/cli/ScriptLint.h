#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace geck {
class Lst;
namespace resource {
    class GameResources;
}

namespace cli {

    struct LintOptions {
        std::string name;      ///< scripts.lst basename (see ScriptIntrospect.h); preferred
        int programIndex = -1; ///< 0-based scripts.lst index; -1 = unset
        bool all = false;      ///< every script with a mounted source instead of one
        /// The scripts a map places (its own and every map_scripts entry), each also checked against
        /// where it is placed and where the map puts its local variables. A VFS path or a file.
        std::string map;
        /// Also report notes (procedures that never run). On for one script, off for `all`, where
        /// they would bury the problems.
        bool includeNotes = true;
        std::string locale = "english"; ///< dialog .msg language
    };

    /// Check scripts for problems the compiler does not catch, each as {severity, check, message, file,
    /// line, procedure}:
    ///  - "syntax" (error): what the preprocessor or parser could not read;
    ///  - "local-vars" (error): a local variable number past the count scripts.lst gives the script
    ///    (`# local_vars=N`). The engine checks only the map's whole array (fallout2-ce map.cc
    ///    mapGetLocalVar), so it silently reads and writes the next script's variables;
    ///  - "messages" (error): a message number its dialog .msg does not have (the engine shows "Error"),
    ///    or a list that names no script; a warning when the .msg itself is missing;
    ///  - "unused-procedure" (note): a procedure written in the script that nothing calls, which sslc
    ///    leaves out of the compiled script.
    /// Emits JSON to `out`; returns 0 when the lint ran (whatever it found), nonzero when the script
    /// cannot be resolved or has no mounted source.
    int lintScripts(resource::GameResources& resources, const LintOptions& options, std::ostream& out);

    /// Lint .ssl files on disk, e.g. while editing them, with what the mounted data allows: a file
    /// whose name is in scripts.lst gets the local variable and message checks too. Prints one
    /// `file:line: severity: message [check]` line per finding (the form compilers use, so editors can
    /// jump to it), or a JSON object with `json`. Returns 1 when any file has an error, else 0.
    int lintFiles(resource::GameResources& resources, const std::vector<std::filesystem::path>& files, bool includeNotes,
        const std::string& locale, bool json, std::ostream& out);

    /// What describe_script's `analyze` adds for one script: its procedures (live, entry point), global
    /// and local variable accesses, the critter fields it sets on itself, its message uses and the lint
    /// findings. nullopt when no .ssl source for the script is mounted.
    std::optional<nlohmann::ordered_json> analyzeScript(resource::GameResources& resources, const Lst& scripts, int programIndex,
        const std::string& locale);

} // namespace cli
} // namespace geck
