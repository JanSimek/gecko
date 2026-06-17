#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

struct AnalyzeOptions {
    // Maps to analyse (VFS paths, e.g. "maps/arroyo.map"). Empty = every map under maps/ in the
    // mounted data.
    std::vector<std::string> maps;
    // Emit machine-readable JSON (for the MCP) instead of the human-readable report.
    bool json = false;
    // Emit only the weighted generation palette ({floor, scenery} with weights), aggregated across
    // the maps — the small input a generator script needs, instead of the full per-map report.
    bool palette = false;
};

// Analyse the ground-tile and object (scenery/wall/critter/...) usage across the given maps and
// write a human-readable report to `out`: a per-map palette (which floor tiles and which protos
// co-occur) plus aggregate histograms. Returns 0 on success, non-zero if no maps were found.
//
// Headless: reads the DAT/VFS through GameResources without ever touching the GL-bound
// TextureManager. Shared by the gecko-cli frontend (and, later, the MCP server).
int analyzeMaps(resource::GameResources& resources, const AnalyzeOptions& options, std::ostream& out);

} // namespace geck::cli
