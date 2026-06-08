#include "Map.h"

#include <string>

#include "format/lst/Lst.h"
#include "format/map/MapObject.h"

namespace geck {

const std::unordered_map<int, std::vector<std::shared_ptr<MapObject>>>& Map::objects() const {
    return mapFile->map_objects;
}

Map::MapFile& Map::getMapFile() {
    return *mapFile;
}

void Map::setMapFile(std::unique_ptr<MapFile> newMapFile) {
    mapFile = std::move(newMapFile);
}

int Map::elevations() const {
    return static_cast<int>(objects().size());
}

} // namespace geck
