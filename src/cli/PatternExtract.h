#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace geck {

namespace resource {
    class GameResources;
}

namespace cli {

    struct ExtractOptions {
        std::string mapPath;
        std::string outPath;
        std::string name;
        int elevation = 0;
        /// Option A: locate the structure by these proto PIDs (the agent reads them from analyze). The
        /// matched objects' bounding box, grown by `radius`, is the capture region.
        std::vector<std::uint32_t> pids;
        /// Explicit anchor/centre hex; if < 0 it is derived from the matched objects.
        int anchorHex = -1;
        /// Hexes of padding around the matched objects, so immediate props (campfire, crates, …) that
        /// aren't in `pids` are still captured.
        int radius = 2;
        /// Capture the floor tiles under the region (for structures whose ground is integral, e.g. a
        /// building's interior floor). Off by default: a stamp usually sits on whatever the map paints.
        bool includeFloor = false;
        /// Capture the roof tiles over the region. A tent/building's roof lives on the roof layer, not as
        /// a scenery object, so without this the structure is captured topless. Independent of the floor.
        bool includeRoof = false;
    };

    /// Capture a region of a real map into a reusable pattern stamp (the JSON the editor's pattern
    /// library reads). Returns 0 on success; nonzero with a message to `out` on failure (map unreadable,
    /// no objects matched, or the file couldn't be written).
    int extractPattern(resource::GameResources& resources, const ExtractOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
