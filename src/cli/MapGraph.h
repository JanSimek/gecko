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
};

/// The exit-grid connectivity graph: nodes are maps, edges are the exit-grid links between them
/// (aggregated, with a hex count per edge), resolved to names via maps.txt / map.msg
/// (`MapNameResolver`). Edges to the -2 world map / -1 town map and to maps absent from maps.txt are
/// kept and tagged by `kind`. `stats` flags dead-ends (no outgoing map edge) and maps with no
/// incoming map edge (a location's entry points / orphans).
///
/// IMPORTANT: this is only the exit-grid layer — how a location's maps link to each other (intramap
/// elevation changes + intermap edges within a town) and where they hand off to the world map. It is
/// NOT the inter-city travel graph: cities are reached across the world map, so this graph is
/// connected only within a location. The world layer (areas, world positions, distances between
/// places, which maps an area contains) is data/city.txt — a separate reader.
///
/// A per-map exit pass (no full analyze), so it scales to every shipped map. Emits a JSON object to
/// `out`; returns 0 on success, non-zero if no maps were found.
int buildMapGraph(resource::GameResources& resources, const MapGraphOptions& options, std::ostream& out);

} // namespace geck::cli
