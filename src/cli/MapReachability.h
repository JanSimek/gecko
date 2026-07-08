#pragma once

#include <iosfwd>
#include <string>

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
    ///
    /// The flood-fill itself lives in `geck::reachability` (gecko_core) so this serializer and the
    /// editor's "unreachable areas" overlay share one implementation; this function only shapes that
    /// result into JSON.
    int analyzeReachability(resource::GameResources& resources, const ReachabilityOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
