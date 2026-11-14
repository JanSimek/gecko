#pragma once

#include <ostream>
#include <string>

namespace geck::resource {
    class GameResources;
}

namespace geck::cli {

struct StripExitGridsOptions {
    std::string mapPath; // VFS path or a file on disk, per cli::loadMap
    std::string outPath;
};

/// Removes every exit-grid marker object (MapObject::isExitGridMarker()) from every elevation of
/// a map and writes the result to outPath. Nothing else about the map is touched - tiles, scripts,
/// every other object, and their ordering are preserved exactly as read. Returns a process exit
/// code (0 on success).
int stripExitGrids(resource::GameResources& resources, const StripExitGridsOptions& options, std::ostream& out);

} // namespace geck::cli
