#include "cli/MapEditTools.h"

#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "resource/GameResources.h"
#include "resource/MapFileIO.h"

#include <algorithm>

namespace geck::cli {

int stripExitGrids(resource::GameResources& resources, const StripExitGridsOptions& options, std::ostream& out) {
    std::string error;
    auto map = loadMap(resources, options.mapPath, &error);
    if (!map) {
        out << "error: could not load map " << options.mapPath << ": " << error << "\n";
        return 1;
    }

    auto& mapFile = map->getMapFile();
    std::size_t totalRemoved = 0;
    for (auto& [elevation, objects] : mapFile.map_objects) {
        const auto before = objects.size();
        objects.erase(
            std::remove_if(objects.begin(), objects.end(),
                [](const std::shared_ptr<MapObject>& o) { return o && o->isExitGridMarker(); }),
            objects.end());
        const auto removed = before - objects.size();
        if (removed > 0) {
            out << "elevation " << elevation << ": removed " << removed << " exit grid marker(s)\n";
        }
        totalRemoved += removed;
    }

    if (totalRemoved == 0) {
        out << "no exit grid markers found - nothing to remove\n";
    }

    if (!resource::writeMap(resources, *map, options.outPath).has_value()) {
        out << "error: failed to write map: " << options.outPath << "\n";
        return 1;
    }

    out << "removed " << totalRemoved << " exit grid marker(s) total; wrote " << options.outPath << "\n";
    return 0;
}

} // namespace geck::cli
