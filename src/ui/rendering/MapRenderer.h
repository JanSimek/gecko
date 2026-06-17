#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Image.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace geck {

class Map;

namespace resource {
    class GameResources;
}

/// Headless map → image renderer (Qt-free). Two styles:
///
///  - Natural: builds the map's sprites with MapSpriteLoader and draws them through the same
///    RenderingEngine the editor uses — what the map *looks like*.
///  - Schematic: flat-colours each floor tile by its tile-id and marks objects by category, and
///    fills a Legend (colour → id/type → count). This grounds the analyze JSON to the picture: the
///    ids become visible regions and the borders between colours are the floor-tile transitions, so
///    an agent can match what it sees to the data (and curate a transition set for seamless terrain).
///
/// Both render into an off-screen sf::RenderTexture; renderToImage() throws if one can't be created
/// (e.g. a headless box with no display) or the map has nothing to draw, so a frontend can report it.
class MapRenderer {
public:
    enum class Style {
        Natural,
        Schematic,
    };

    struct Options {
        int elevation = 0;
        Style style = Style::Natural;
        /// Longest side of the output image in pixels; the map's content bounds are fit into it.
        unsigned int maxDimension = 1600;
        /// Roof tiles cover the floor, so they are off by default — a biome view wants the ground.
        bool showRoof = false;
        bool showObjects = true;
        /// Schematic only: include FLAT objects (invisible engine blockers — scroll/hex blockers,
        /// exit grids). Off by default so the dots show the real, in-game-visible scenery and walls.
        bool showBlockers = false;
        sf::Color background{ 0, 0, 0, 255 };
    };

    /// The schematic's colour key, so a caller can print the mapping the agent joins back to the
    /// analyze JSON. Floor entries carry the raw tile-id; object entries carry the engine type name.
    struct Legend {
        struct FloorEntry {
            uint16_t id = 0;
            sf::Color color;
            int count = 0;
        };
        struct ObjectEntry {
            std::string type;
            sf::Color color;
            int count = 0;
        };
        std::vector<FloorEntry> floors;
        std::vector<ObjectEntry> objects;
    };

    explicit MapRenderer(resource::GameResources& resources);

    /// Render `map` at `options.elevation` to an RGBA image. In Schematic style, `legend` (when
    /// non-null) is filled with the colour key. Throws std::runtime_error when the map has nothing
    /// to draw at that elevation, or when no off-screen GL context is available.
    sf::Image renderToImage(Map& map, const Options& options, Legend* legend = nullptr);

private:
    sf::Image renderNatural(Map& map, const Options& options);
    sf::Image renderSchematic(Map& map, const Options& options, Legend* legend);

    resource::GameResources& _resources;
};

} // namespace geck
