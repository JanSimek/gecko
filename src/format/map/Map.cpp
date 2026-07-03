#include "Map.h"

#include <string>

#include "format/lst/Lst.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "util/Constants.h"

namespace geck {

const std::unordered_map<int, std::vector<std::shared_ptr<MapObject>>>& Map::objects() const {
    return mapFile->map_objects;
}

Map::MapFile& Map::getMapFile() {
    return *mapFile;
}

const Map::MapFile& Map::getMapFile() const {
    return *mapFile;
}

void Map::setMapFile(std::unique_ptr<MapFile> newMapFile) {
    mapFile = std::move(newMapFile);
}

int Map::elevations() const {
    return static_cast<int>(objects().size());
}

std::vector<Tile> Map::createEmptyElevation() {
    std::vector<Tile> tiles;
    tiles.reserve(TILES_PER_ELEVATION);
    for (unsigned int i = 0; i < TILES_PER_ELEVATION; ++i) {
        tiles.emplace_back(EMPTY_TILE, EMPTY_TILE); // EMPTY_TILE for both floor and roof
    }
    return tiles;
}

Map::MapFile Map::createEmptyMapFile() {
    MapFile mapFile;

    mapFile.header.version = FileFormat::FALLOUT2_MAP_VERSION;
    mapFile.header.filename = "newmap";
    mapFile.header.player_default_position = MapDefaults::PLAYER_DEFAULT_POSITION;
    mapFile.header.player_default_elevation = MapDefaults::PLAYER_DEFAULT_ELEVATION;
    mapFile.header.player_default_orientation = MapDefaults::PLAYER_DEFAULT_ORIENTATION;
    mapFile.header.num_local_vars = 0;
    mapFile.header.script_id = MapDefaults::NO_SCRIPT_ID;
    mapFile.header.flags = MapDefaults::DEFAULT_FLAGS; // all elevations enabled
    mapFile.header.darkness = MapDefaults::NO_DARKNESS;
    mapFile.header.num_global_vars = 0;
    mapFile.header.map_id = MapDefaults::DEFAULT_MAP_ID;
    mapFile.header.timestamp = MapDefaults::DEFAULT_TIMESTAMP;

    for (int elevation = ELEVATION_1; elevation <= ELEVATION_3; ++elevation) {
        mapFile.tiles[elevation] = createEmptyElevation();
        mapFile.map_objects[elevation].clear();
    }

    for (int section = 0; section < SCRIPT_SECTIONS; ++section) {
        mapFile.map_scripts[section].clear();
        mapFile.scripts_in_section[section] = 0;
    }

    return mapFile;
}

} // namespace geck
