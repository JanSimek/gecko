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
    /// outward from the **entry points** — the player start plus every exit grid (you arrive at an
    /// exit's position when entering from the adjacent map) — and reports how much is reachable
    /// (`reachableHexes`) and any critter/item cut off from every entry (`orphanedObjects`). Each exit
    /// grid additionally carries `reachableFromPlayerStart`: whether it shares a walkable component
    /// with the player start specifically (null when the player spawns on another elevation).
    /// Elevations with no entry point (reached via stairs/elevators) report `reachableHexes: null` +
    /// a note.
    ///
    /// This is **optimistic** reachability, not exact pathfinder truth: a hex is blocked per the
    /// engine's instance-flag rule (a CRITTER/SCENERY/WALL that is neither hidden nor flagged
    /// no-block; items never block), but **doors are treated as passable** (including locked ones —
    /// the player can open them), so the result over-estimates rather than crying wolf.
    ///
    /// Emits a JSON object to `out`; returns 0 on success, nonzero with a message on a hard error
    /// (map fails to load).
    int analyzeReachability(resource::GameResources& resources, const ReachabilityOptions& options, std::ostream& out);

    // --- Pure helpers, exposed for testing (no game data needed) ---

    /// The engine's instance-flag movement-blocking rule (fallout2-ce `_obj_blocking_at`): a
    /// CRITTER/SCENERY/WALL object that is neither `OBJECT_HIDDEN` nor `OBJECT_NO_BLOCK` in its
    /// per-instance `flags`. Items, misc and tiles never block. (Doors, though scenery, are handled
    /// separately as passable.)
    bool blocksMovementByInstance(uint32_t objectType, uint32_t flags);

    /// The up-to-6 parity-correct neighbour positions of a hex (empty if `hex` is off-grid).
    std::vector<int> hexNeighbors(int hex);

    /// Connected components of the WIDTH*HEIGHT hex grid given a `blocked` mask (1 = impassable).
    /// Returns a component id per hex (-1 for blocked hexes); `sizes[id]` is each component's hex
    /// count and `samples[id]` is one representative hex in it.
    std::vector<int> hexComponents(const std::vector<char>& blocked, std::vector<int>& sizes, std::vector<int>& samples);

} // namespace cli
} // namespace geck
