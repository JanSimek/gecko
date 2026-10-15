#pragma once

#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace geck {
class Lst;
namespace resource {
    class GameResources;
}

/// Finding a script by name or program index and describing it, shared by the script tools
/// (describe_script, find_script, lint_script, script_refs). See ScriptIntrospect.h on index bases.
namespace cli::script_lookup {

    std::string toLower(std::string s);

    /// Basename of a path without directory or extension ("scripts_src/newreno/ncprosti.ssl" -> "ncprosti").
    std::string stem(const std::string& path);

    /// basename (lowercased) -> VFS path for every *.ssl in the mounted data (a mounted source tree).
    std::unordered_map<std::string, std::string> indexSslSources(resource::GameResources& resources);

    /// The mounted scripts.lst, or nullptr.
    const Lst* loadScriptsLst(resource::GameResources& resources);

    /// The identity block every script-shaped result leads with. `sslConstant` is the 1-based number
    /// the FRP headers/scripts.h SCRIPT_* constants carry, echoed next to the 0-based programIndex so
    /// the two bases can be told apart on sight instead of by silently naming the wrong script.
    nlohmann::ordered_json scriptIdentity(resource::GameResources& resources, const Lst& scripts, int programIndex);

    /// The outcome of turning a caller's `name`/`programIndex` into one scripts.lst entry: exactly one
    /// index, or a candidate list when a name fragment was ambiguous, or a hard error.
    struct Resolution {
        int programIndex = -1;       ///< >= 0 when a single script was selected
        std::vector<int> candidates; ///< non-empty when `name` matched several entries
        std::string error;           ///< non-empty when nothing matched at all
        bool fromFragment = false;   ///< resolved from a substring, not an exact basename
    };

    /// Resolve by name (exact basename first, then substring — so a half-remembered name still helps)
    /// or, failing that, by 0-based program index.
    Resolution resolveScript(const Lst& scripts, const std::string& name, int programIndex);

    /// A candidate-list result: the caller gave a fragment that names several scripts, so report them
    /// rather than picking one.
    nlohmann::ordered_json ambiguousResult(resource::GameResources& resources, const Lst& scripts, const std::string& query,
        const std::vector<int>& candidates);

    /// Write `root` as one line of JSON. Fallout 2 text is CP-1252, not UTF-8, so stray bytes become
    /// U+FFFD instead of making dump() throw on e.g. 0x85 ("…").
    void emit(std::ostream& out, const nlohmann::ordered_json& root);

} // namespace cli::script_lookup
} // namespace geck
