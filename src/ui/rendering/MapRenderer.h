#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Image.hpp>

namespace geck {

class Map;

namespace resource {
    class GameResources;
}

/// Headless map → image renderer (Qt-free). Builds the map's sprites with MapSpriteLoader and draws
/// them through the same RenderingEngine the editor uses, into an off-screen sf::RenderTexture, then
/// returns the pixels as an sf::Image. Lets gecko-cli and the MCP server export a PNG of a map — and
/// gives an agent a way to *see* a generated biome, not just read its statistics.
///
/// Needs an off-screen GL context (sf::RenderTexture); renderToImage() throws if one can't be created
/// (e.g. a headless Linux box with no display), so the frontend can report it rather than crash.
class MapRenderer {
public:
    struct Options {
        int elevation = 0;
        /// Longest side of the output image in pixels; the map's content bounds are fit into it.
        unsigned int maxDimension = 1600;
        /// Roof tiles cover the floor, so they are off by default — a biome view wants the ground.
        bool showRoof = false;
        bool showObjects = true;
        /// Editor-only debug overlays (blockers, hex grid) are off: this renders the map's artwork.
        sf::Color background{ 0, 0, 0, 255 };
    };

    explicit MapRenderer(resource::GameResources& resources);

    /// Render `map` at `options.elevation` to an RGBA image. Throws std::runtime_error when the map
    /// has nothing to draw at that elevation, or when no off-screen GL context is available.
    sf::Image renderToImage(Map& map, const Options& options);

private:
    resource::GameResources& _resources;
};

} // namespace geck
