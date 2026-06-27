#pragma once

#include <iosfwd>
#include <string>

namespace geck {

namespace resource {
    class GameResources;
}

namespace cli {

    struct RenderOptions {
        std::string mapPath;
        std::string outPath;
        int elevation = 0;
        /// Longest side of the output PNG in pixels; the map's content bounds are fit into it.
        unsigned int maxDimension = 1600;
        bool showRoof = false;
        bool showObjects = true;
        /// Schematic style: flat-colour floor tiles by id + mark objects by category, and print the
        /// colour legend so the agent can match the picture to the analyze JSON.
        bool schematic = false;
        /// Objects style: muted-grey floor + category-coloured object markers, for verifying scatter.
        bool objects = false;
        /// Semantic style: grey floor + markers coloured by role (exit grids highlighted, critters by
        /// team, scripted objects ringed) — the map's purpose layer, paired with describe_map.
        bool semantic = false;
        /// Schematic/objects only: also mark FLAT objects (invisible engine blockers); off by default.
        bool showBlockers = false;
        /// Frame to the full map screen extent (the whole iso playable grid) instead of cropping to
        /// content, so sparse/empty maps still show the entire grid; off by default.
        bool fullExtent = false;
    };

    /// Render a map to an image file (format inferred from the extension, e.g. .png). Returns 0 on
    /// success; nonzero with a message to `out` on failure — map unreadable, no off-screen GL context
    /// (headless without a display), or the file couldn't be written.
    int renderMap(resource::GameResources& resources, const RenderOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
