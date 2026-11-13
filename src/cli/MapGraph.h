#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

struct MapGraphOptions {
    // Maps to include (VFS paths, e.g. "maps/arroyo.map"). Empty = every map in the mounted data.
    std::vector<std::string> maps;
    // Add the script-driven transitions (load_map) when a script-source tree is mounted.
    bool scripts = true;
};

/// The exit-grid connectivity graph: nodes are maps, edges are the exit-grid links between them
/// (aggregated, with a hex count per edge), resolved to names via maps.txt / map.msg
/// (`MapNameResolver`). Edges to the -2 world map / -1 town map and to maps absent from maps.txt are
/// kept and tagged by `kind`. `stats` flags dead-ends (no outgoing map edge) and maps with no
/// incoming map edge (a location's entry points / orphans).
///
/// Each map-kind edge is additionally crossed with the `geck::reachability` model to flag
/// **one-way** links: `oneWay` is true when the player cannot walk back (`oneWayReason` =
/// "no-return-edge" when the destination has no exit grid targeting the source, or
/// "return-unreachable" when return exits exist but none shares a walkable component with the
/// arrival hexes on the arrival elevation), false when a return path exists, and null when
/// undeterminable (destination never analysed, or a return might run through another elevation —
/// stairs are not traced, so "return-unreachable" is claimed only when every return exit lies on
/// an arrival elevation). `stats.oneWayEdges` lists the flagged edges.
///
/// IMPORTANT: this is only the exit-grid layer — how a location's maps link to each other (intramap
/// elevation changes + intermap edges within a town) and where they hand off to the world map. It is
/// NOT the inter-city travel graph: cities are reached across the world map, so this graph is
/// connected only within a location. The world layer (areas, world positions, distances between
/// places, which maps an area contains) is data/city.txt — a separate reader.
///
/// **Script transitions** (`options.scripts`, needs a mounted .ssl source tree): scripts move the
/// player between maps with load_map — caravans, escorts, dialogue outcomes — which no exit grid
/// shows. Each call is joined to the maps that place its script (the map's own script and its
/// map_scripts entries, matched to scripts.lst by base name) and listed in `scriptEdges` per
/// (from, to) with the calls behind it; a call whose script no analysed map places goes to
/// `unplacedScriptCalls`. They are conditional (the code runs on some dialogue node or event), so
/// each call carries script/procedure/line to read the condition, and `entrance` (the value the
/// engine stores in GVAR_LOAD_MAP_INDEX for the target's map script to pick an arrival point). The
/// exit-grid `edges`, `deadEnds`, `noIncoming` and one-way analysis are unchanged; `stats` adds
/// `scriptsRead` (0 = no sources mounted, so no script edges) and `noIncomingExceptScript`: maps no
/// other map's exit grid enters but a script transition does (a world-map entrance may too).
///
/// A per-map exit pass (no full analyze), so it scales to every shipped map. Emits a JSON object to
/// `out`; returns 0 on success, non-zero if no maps were found.
int buildMapGraph(resource::GameResources& resources, const MapGraphOptions& options, std::ostream& out);

} // namespace geck::cli
