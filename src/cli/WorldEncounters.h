#pragma once

#include <iosfwd>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// The worldmap's terrain types and random-encounter group tables, from data/worldmap.txt: each
/// terrain type (name, short name, draw weight) and each `[Encounter: NAME]` group with its spawn
/// entries (proto pid, ratio, dead flag, script, carried items). The shipped encounter names are
/// region-prefixed (ARRO_*, KLA_*, …) — the worldmap's "location sections" (which encounters belong
/// to which area). Pids are raw — resolve them with proto_info. Emits a JSON object to `out`; returns
/// 0 on success, non-zero if worldmap.txt isn't mounted.
int buildWorldEncounters(resource::GameResources& resources, std::ostream& out);

} // namespace geck::cli
