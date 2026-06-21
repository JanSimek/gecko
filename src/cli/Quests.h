#pragma once

#include <iosfwd>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// The quest registry from data/quests.txt, the "what the player has to do" layer. Each quest with:
/// its 'area' (the location name from map.msg), the global variable that tracks it ('gvar' index +
/// 'gvarName' GVAR_* + 'gvarStart' default, resolved via vault13.gam), the 'displayThreshold' /
/// 'completedThreshold' gvar values that gate it, and the 'description' from quests.msg. An agent can
/// join 'area' to world_map area names, 'gvarName' to scripts (which set/check it), and read the
/// description for the objective. Emits a JSON object to `out`; returns 0 on success, non-zero if
/// quests.txt isn't mounted.
int buildQuests(resource::GameResources& resources, std::ostream& out);

} // namespace geck::cli
