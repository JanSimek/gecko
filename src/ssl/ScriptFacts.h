#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ssl/Preprocessor.h"

namespace geck::ssl {

/// What a script's SSL source does that the editor and the analysis tools report.
struct ScriptFacts {
    /// A critter field the script assigns to its own object, replacing whatever value the map stores.
    struct Override {
        enum class Field {
            AiPacket, // critter_add_trait(self_obj, TRAIT_OBJECT, OBJECT_AI_PACKET, ..), e.g. via set_self_ai
            Team,     // critter_add_trait(self_obj, TRAIT_OBJECT, OBJECT_TEAM_NUM, ..), e.g. via set_self_team
        };
        Field field = Field::AiPacket;
        std::string procedure;      ///< the procedure the call runs in, e.g. "map_enter_p_proc"
        std::filesystem::path file; ///< where the call is written (the script, or a header it includes)
        int line = 0;               ///< 1-based, in `file`
        std::optional<int> value;   ///< the value assigned, when it is a constant
        std::string valueName;      ///< the macro the value was written as, e.g. "AI_ARROYO_VILLAGER"
    };

    std::filesystem::path script; ///< the script file, as Override::file names it when written there
    std::vector<Override> overrides;
    /// Local variable names from `#define LVAR_<name> (<index>)` in the script and the headers it
    /// includes, by index. Where both name an index, the script's own name wins.
    std::map<int, std::string> localVarNames;
    /// Preprocessing problems as "file:line: message", e.g. an #include that could not be found.
    std::vector<std::string> problems;

    bool assigns(Override::Field field) const;
};

/// Preprocess the script at `path` (following its #includes through `provider`) and read the facts
/// above out of the result. Calls are matched after macro expansion, so any macro spelling of the
/// opcode is found, and a call inside a comment, a string or a macro that is never used is not.
ScriptFacts inspectScript(const SourceProvider& provider, const std::filesystem::path& path, const PreprocessOptions& options = {});

} // namespace geck::ssl
