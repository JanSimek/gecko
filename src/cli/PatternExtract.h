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
        /// Capture the floor/roof tiles under the region too (for structures whose floor is integral).
        bool includeFloor = false;
    };

    /// Capture a region of a real map into a reusable pattern stamp (the JSON the editor's pattern
    /// library reads). Returns 0 on success; nonzero with a message to `out` on failure (map unreadable,
    /// no objects matched, or the file couldn't be written).
    int extractPattern(resource::GameResources& resources, const ExtractOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
