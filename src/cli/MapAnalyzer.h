#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace geck {
class Map;
}

namespace geck::resource {
class GameResources;
class DataFileSystem;
}

namespace geck::cli {

// One exit-grid marker's link: where it sits (srcHex on srcElevation) and where it leads (destMap is
// a maps.txt index, or the -1 town / -2 worldmap sentinels). The raw data behind analyze's `exits`
// and the map_graph connectivity tool.
struct MapExit {
    int srcHex = 0;
    int srcElevation = 0;
    int destMap = 0;
    int destHex = 0;
    int destElevation = 0;
    int orientation = 0;
};

// Every exit-grid marker in the map, across elevations. Shared by analyze (named per-exit JSON) and
// the map_graph tool (cross-map connectivity).
std::vector<MapExit> collectMapExits(Map& map);

// All ".map" paths under the mounted data (sorted, case-insensitive match), e.g. "maps/arroyo.map".
std::vector<std::string> listMapPaths(const resource::DataFileSystem& files);

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

struct DumpGridOptions {
    // The single map to dump (VFS path, e.g. "maps/desert6.map"), required.
    std::string map;
    // Which elevation to dump; -1 = every present elevation.
    int elevation = -1;
    bool floor = true;   // emit the floor-tile id grid
    bool roof = false;   // emit the roof-tile id grid
    bool objects = true; // emit per-object {pid,number,type,name,hex,col,row,dir,flat}
};

// Dump the RAW spatial layout of one map as JSON: per elevation, the floor (and optionally roof)
// tile-id grid (row-major, COLS wide; `emptyTile` marks empty) and every object's pid/number/type/
// name and hex position (with col/row + facing). Where `analyze` gives digested adjacency and
// object clusters, this is the underlying per-cell data — for learning exact tile placement,
// transition masks and scatter density/positions from real maps. Returns 0 on success, non-zero if
// the map can't be read. Headless, like analyzeMaps.
int dumpMapGrid(resource::GameResources& resources, const DumpGridOptions& options, std::ostream& out);

} // namespace geck::cli
