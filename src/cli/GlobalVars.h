#pragma once

#include <iosfwd>

namespace geck {
class Gam;
}

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// Load data/vault13.gam (the GAME_GLOBAL_VARS dictionary) through the repository cache, or nullptr
/// if it isn't mounted. Shared by the gvars tool and the quests tool (gvar name/default resolution).
Gam* loadGameGam(resource::GameResources& resources);

/// The global-variable dictionary, from data/vault13.gam: every GVAR by index → name + default
/// value. Global variables are the engine's progression state machine, so this lets an agent decode
/// a gvar index (from quests, scripts, or endings) to its GVAR_* name. Emits a JSON object to `out`;
/// returns 0 on success, non-zero if vault13.gam isn't mounted.
int buildGlobalVars(resource::GameResources& resources, std::ostream& out);

} // namespace geck::cli
