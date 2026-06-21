#pragma once

#include <iosfwd>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// Find where a global variable is used in the mounted .ssl / .h script sources: every script that
/// reads or writes `gvarName`, with the line number, the source line, and whether it is a 'set'
/// (set_global_var — the in-game action that advances/gates a quest) or a 'get' (a condition check).
/// Headers are scanned too because many gvars are only ever touched through macro aliases there
/// (e.g. caravan.h `#define set_caravan_brahmin(x) set_global_var(GVAR_CARAVAN_BRAHMIN_TOTAL,x)`);
/// `//` line comments are ignored.
///
/// This is the causal link the other tools point to: a quest's gvar (from `quests`) → the scripts
/// that change it → `describe_script` for the dialogue/logic behind it. Needs a script-source tree
/// (e.g. the FRP `scripts_src`) mounted as a --data path — compiled .int scripts carry no names.
///
/// Emits a JSON object to `out`. Returns 0 on success — including the valid "gvar is unused" case when
/// sources were scanned — and non-zero only when no .ssl/.h source tree is mounted.
int findGvarRefs(resource::GameResources& resources, const std::string& gvarName, std::ostream& out);

} // namespace geck::cli
