#pragma once

#include <iosfwd>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

struct HexSightlineOptions {
    int fromHex = -1;
    int toHex = -1;
    /// Optional map (VFS path or file on disk, including a save slot's gzip .SAV). With it, the straight
    /// path reports the first object that blocks it, using the engine's _obj_blocking_at rules.
    std::string mapPath;
    int elevation = 0;
    /// Optional rotation 0..5 of the watcher at fromHex; reports whether toHex is in its frontal arc.
    int facing = -1;
    /// Optional view centre for the screen-space math; defaults to fromHex (see cli/EngineTile.h).
    int cameraHex = -1;
    /// Walk as the sfall ObjCanSeeObj_ShootThru_Fix does (a6 == 32): objects flagged ShootThru don't block.
    bool shootThrough = false;
};

/// Engine-exact geometry between two hexes: tile_dist distance, tile_dir rotations both ways, the
/// _can_see frontal-arc test for an optional facing, and make_straight_path's walk with its first
/// blocker when a map is given — the pieces obj_can_see_obj combines. Emits a JSON object to `out`;
/// returns 0 on success, nonzero on bad hexes or an unreadable map.
int analyzeHexSightline(resource::GameResources& resources, const HexSightlineOptions& options, std::ostream& out);

} // namespace geck::cli
