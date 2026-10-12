#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace geck::resource {

/// What a script's SSL source does that the editor should tell the user about.
struct ScriptSourceFacts {
    /// A critter field the script assigns to its own object, which replaces whatever value the map
    /// stores for it.
    struct Override {
        enum class Field {
            AiPacket, // critter_add_trait(self_obj, TRAIT_OBJECT, OBJECT_AI_PACKET, ..) / set_self_ai
            Team,     // critter_add_trait(self_obj, TRAIT_OBJECT, OBJECT_TEAM_NUM, ..) / set_self_team
        };
        Field field;
        std::string procedure; // the procedure the assignment sits in, e.g. "map_enter_p_proc"
        int line = 0;          // 1-based line in the source
    };

    std::vector<Override> overrides;
    /// Local variable names from `#define LVAR_<Name> (<index>)`, by index.
    std::map<int, std::string> localVarNames;

    bool assigns(Override::Field field) const;
};

/// Reads the facts above out of SSL source text. Comments are ignored, so the commented-out
/// template lines most scripts carry (`// critter_add_trait(...OBJECT_AI_PACKET...)`) don't count.
ScriptSourceFacts inspectScriptSource(std::string_view source);

} // namespace geck::resource
