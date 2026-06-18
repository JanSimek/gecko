#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace geck {
namespace resource {
    class GameResources;
}

namespace cli {

    struct ReachabilityOptions {
        std::string mapPath;
    };

    /// Per-elevation reachability for one map. For each present elevation it floods the walkable hexes
    /// (doors count as passable — the player can open them) outward from the entry points (player
    /// start + exit grids) and reports how much is reachable, any critter/item cut off from it
    /// (`orphanedObjects`), and per exit grid whether the player can walk to it from the start
    /// (`exits[].reachableFromPlayerStart`). Elevations with no entry point (reached via
    /// stairs/elevators) report a note and skip orphan detection. Emits a JSON object to `out`;
    /// returns 0 on success, nonzero with a message on a hard error (map fails to load).
    int analyzeReachability(resource::GameResources& resources, const ReachabilityOptions& options, std::ostream& out);

    // --- Pure hex-grid graph helpers, exposed for testing (no game data needed) ---

    /// The up-to-6 parity-correct neighbour positions of a hex (empty if `hex` is off-grid).
    std::vector<int> hexNeighbors(int hex);

    /// Connected components of the WIDTH*HEIGHT hex grid given a `blocked` mask (1 = impassable).
    /// Returns a component id per hex (-1 for blocked hexes); `sizes[id]` is each component's hex
    /// count and `samples[id]` is one representative hex in it.
    std::vector<int> hexComponents(const std::vector<char>& blocked, std::vector<int>& sizes, std::vector<int>& samples);

} // namespace cli
} // namespace geck
