#pragma once

#include <iosfwd>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// The worldmap's terrain types and random-encounter data, from data/worldmap.txt: each terrain type
/// (name, short name, draw weight), the percentage each encounter frequency rolls, each `[Encounter:
/// NAME]` group with its spawn entries (proto pid, ratio, dead flag, script, carried items), and each
/// `[Encounter Table N]` with its maps and weighted entries. The shipped encounter names are
/// region-prefixed (ARRO_*, KLA_*, …) — the worldmap's "location sections" (which encounters belong
/// to which area). Pids are raw — resolve them with proto_info. Emits a JSON object to `out`; returns
/// 0 on success, non-zero if worldmap.txt isn't mounted.
int buildWorldEncounters(resource::GameResources& resources, std::ostream& out);

/// The encounter odds at worldmap pixel (x, y): its tile and sub-tile, terrain, fill, the frequency
/// and percentage for each part of the day, and the encounter table rolled there. Emits a JSON object
/// to `out`; returns 0 on success, non-zero if worldmap.txt isn't mounted or (x, y) is off the map.
int describeWorldPosition(resource::GameResources& resources, int x, int y, std::ostream& out);

} // namespace geck::cli
