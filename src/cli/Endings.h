#pragma once

#include <iosfwd>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// The endgame slideshow's win-condition table, from data/endgame.txt: each slide with the global
/// variable + value that triggers it ('gvar' + 'gvarName' via vault13.gam + 'value', plus a readable
/// 'condition' string), the slide image ('art'), and the narrator/subtitle base name ('narrator').
/// Slides sharing a gvar at different values are a location's branching outcomes — so this is "what
/// world state must be reached for each ending." Closes the start→objectives→ending loop with the
/// quests and gvars tools. Emits a JSON object to `out`; returns 0 on success, non-zero if
/// endgame.txt isn't mounted.
int buildEndings(resource::GameResources& resources, std::ostream& out);

} // namespace geck::cli
