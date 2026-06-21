#pragma once

#include <iosfwd>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// Find where a global variable is used in the mounted .ssl script sources: every script that reads
/// or writes `gvarName`, with the line number, the source line, and whether it is a 'set'
/// (set_global_var — the in-game action that advances/gates a quest) or a 'get' (a condition check).
///
/// This is the causal link the other tools point to: a quest's gvar (from `quests`) → the scripts
/// that change it → `describe_script` for the dialogue/logic behind it. Needs a script-source tree
/// (e.g. the FRP `scripts_src`) mounted as a --data path — compiled .int scripts carry no names.
///
/// Emits a JSON object to `out`; returns 0 on success, non-zero if no sources are mounted or the gvar
/// is never referenced.
int findGvarRefs(resource::GameResources& resources, const std::string& gvarName, std::ostream& out);

} // namespace geck::cli
