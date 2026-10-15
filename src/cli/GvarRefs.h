#pragma once

#include <iosfwd>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// Find where a global variable is used in the mounted .ssl script sources: every read
/// (`global_var`) and write (`set_global_var` — the in-game action that advances or gates a quest)
/// of `gvarName` in code a compiled script runs. Each script is preprocessed and parsed (ssl/), so an
/// access through a macro alias counts where the script uses the alias (`via` names it, e.g.
/// caravan.h's set_caravan_brahmin), and commented-out code, unused macros, procedures nothing calls
/// and branches on a constant do not. A ref names its script, procedure, line (in `file` when that is
/// a header or template the script includes) and source text.
///
/// This is the causal link the other tools point to: a quest's gvar (from `quests`) → the scripts
/// that change it → `describe_script` for the dialogue/logic behind it. Needs a script-source tree
/// (e.g. the FRP `scripts_src`) mounted as a --data path — compiled .int scripts carry no names.
///
/// Emits a JSON object to `out`. Returns 0 on success — including the valid "gvar is unused" case when
/// sources were read — and non-zero only when no .ssl source is mounted.
int findGvarRefs(resource::GameResources& resources, const std::string& gvarName, std::ostream& out);

} // namespace geck::cli
