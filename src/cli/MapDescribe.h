#pragma once

#include <ostream>
#include <string>

namespace geck {

namespace resource {
    class GameResources;
}

namespace cli {

    struct DescribeMapOptions {
        std::string mapPath; // VFS path, e.g. "maps/artemple.map"
    };

    /// One structured digest for a single map, composed from the existing semantic tools: the
    /// analyze per-map report (header digest, floor/biome, object clusters, critter roster with
    /// ai.txt-resolved AI and attached scripts, exits graph) **plus** a `reachability` field
    /// (per-elevation walkable/reachable hexes and any entry-orphaned objects).
    ///
    /// It gathers the engine's own semantic evidence in one call — cross-referencing keys are
    /// preserved (pid, script_id, ai_packet) — so an agent can infer the map's *purpose* from the
    /// same data a designer reads, rather than the tool baking in classification heuristics.
    ///
    /// Emits a JSON object to `out`; returns 0 on success, nonzero on a hard error (e.g. the map
    /// fails to load). Reachability is best-effort: if it can't be computed the digest still emits
    /// with `reachability: null`.
    int describeMap(resource::GameResources& resources, const DescribeMapOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
