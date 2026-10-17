#pragma once

#include <iosfwd>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// The companions, from data/party.txt: each party member's proto pid and name, the combat options the
/// player may set for it (area attack, who to attack, best weapon, chem use, distance, run away,
/// disposition), and its level-up schedule with the proto each level swaps to. Member 0 (the player)
/// is left out, as the engine ignores it. Emits a JSON object to `out`; returns 0 on success, non-zero
/// if party.txt isn't mounted.
int buildParty(resource::GameResources& resources, std::ostream& out);

} // namespace geck::cli
