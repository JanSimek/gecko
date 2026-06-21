#pragma once

#include <iosfwd>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// The worldmap layer, from data/city.txt: every area (a city / town / encounter location) with its
/// name, worldmap position, size, known-at-start flag and the maps it contains (its entrances), plus
/// the straight-line distance between every pair of areas. This is the inter-city travel layer that
/// the exit-grid graph (map_graph) does not cover — the player crosses the world map to get between
/// these areas. Emits a JSON object to `out`; returns 0 on success, non-zero if city.txt isn't
/// mounted.
int buildWorldMap(resource::GameResources& resources, std::ostream& out);

} // namespace geck::cli
