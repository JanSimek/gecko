#pragma once

#include <iosfwd>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

struct WorldMapOptions {
    /// When set, the worldmap is also drawn to this PNG instead of only being described as JSON.
    std::string renderPath;
    /// Draw the city circles (the engine's translucent green markers). Off gives the bare terrain.
    bool markers = true;
};

/// Renders the worldmap exactly as the game draws it — the `art/intrface/wrldmp*` tile grid with the
/// area circles blended over it through the engine's own palette blend tables — to a PNG. Returns 0
/// on success. Reports the image size to `out`.
int renderWorldMap(resource::GameResources& resources, const WorldMapOptions& options, std::ostream& out);

/// The worldmap layer, from data/city.txt: every area (a city / town / encounter location) with its
/// name, worldmap position, size, known-at-start flag and the maps it contains (its entrances), plus
/// the straight-line distance between every pair of areas. This is the inter-city travel layer that
/// the exit-grid graph (map_graph) does not cover — the player crosses the world map to get between
/// these areas. Emits a JSON object to `out`; returns 0 on success, non-zero if city.txt isn't
/// mounted.
int buildWorldMap(resource::GameResources& resources, std::ostream& out);

} // namespace geck::cli
