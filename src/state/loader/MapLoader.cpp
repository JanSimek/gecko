#include "MapLoader.h"
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include "../../util/Constants.h"

#include "../../reader/ReaderFactory.h"
#include "../../reader/map/MapReader.h"

#include "../../format/pro/Pro.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapObject.h"
#include "../../format/map/Tile.h"
#include "../../format/lst/Lst.h"
#include "../../format/frm/Direction.h"
#include "../../format/frm/Direction.h"

#include "../../util/ProHelper.h"
#include "../../util/ResourceManager.h"

namespace geck {

MapLoader::MapLoader(const std::filesystem::path& mapFile, int elevation, std::function<void(std::unique_ptr<Map>)> onLoadCallback)
    : _mapPath(mapFile)
    , _elevation(elevation)
    , _onLoadCallback(onLoadCallback) {
}

void MapLoader::load() {

    if (_mapPath.empty()) {
        spdlog::error("MapLoader: No map path provided");
        return;
    }

    spdlog::stopwatch stopwatch_chunk;
    spdlog::stopwatch stopwatch_total;

    setStatus("Loading map " + _mapPath.filename().string());

    // TODO: move to a new loader that is called only once per application start
    for (const auto& lst_path : { "art/items/items.lst", "art/critters/critters.lst", "art/scenery/scenery.lst", "art/walls/walls.lst", "art/tiles/tiles.lst", "art/misc/misc.lst", "art/intrface/intrface.lst", "art/inven/inven.lst" }) {
        ResourceManager::getInstance().loadResource<Lst>(lst_path);
    }

    setProgress("Parsing map file");

    // MapReader requires callback in constructor, so we need to create it directly
    auto proLoadCallback = [&](uint32_t PID) {
        return ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(PID));
    };
    MapReader map_reader{ proLoadCallback };
    _map = map_reader.openFile(_mapPath);

    if (_elevation == INVALID_ELEVATION) {
        uint32_t default_elevation = _map->getMapFile().header.player_default_elevation;
        spdlog::info("Using default map elevation {}", default_elevation);
        _elevation = default_elevation;
    }

    spdlog::info("... map file parsed in {:.3} seconds", stopwatch_chunk);
    stopwatch_chunk.reset();

    // Tiles
    auto lst = ResourceManager::getInstance().loadResource<Lst>("art/tiles/tiles.lst");

    //    auto tiles = _map->tiles().at(_elevation);
    //    for (auto tileNumber = 0U; tileNumber < geck::Map::TILES_PER_ELEVATION; tileNumber++) {
    //        setProgress("Loading map tile texture " + std::to_string(tileNumber + 1) + " of " + std::to_string(geck::Map::TILES_PER_ELEVATION));
    //        auto tile = tiles.at(tileNumber);
    //        if (tile.getFloor() != Map::EMPTY_TILE) {
    //            ResourceManager::getInstance().insert("art/tiles/" + lst->at(tile.getFloor()));
    //        }
    //        if (tile.getRoof() != Map::EMPTY_TILE) {
    //            ResourceManager::getInstance().insert("art/tiles/" + lst->at(tile.getRoof()));
    //        }
    //    }

    // Tiles
    size_t tile_number = 1;
    size_t tiles_total = lst->list().size();

    for (const auto& tile : lst->list()) {
        setProgress("Loading map tile texture " + std::to_string(tile_number++) + " of " + std::to_string(tiles_total));
        ResourceManager::getInstance().insertTexture("art/tiles/" + tile);
    }

    spdlog::info("... tile textures loaded in {:.3} seconds", stopwatch_chunk);
    stopwatch_chunk.reset();

    // Objects
    size_t objectNumber = 1;
    size_t objectsTotal = _map->objects().at(_elevation).size();

    for (const auto& object : _map->objects().at(_elevation)) {

        setProgress("Loading map object " + std::to_string(objectNumber++) + " of " + std::to_string(objectsTotal));

        if (object->position == -1)
            continue; // object inside an inventory/container

        const std::string frmName = ResourceManager::getInstance().FIDtoFrmName(object->frm_pid);

        ResourceManager::getInstance().insertTexture(frmName);
    }

    ResourceManager::getInstance().insertTexture("art/tiles/blank.frm");

    spdlog::info("... objects loaded in {:.3} seconds", stopwatch_chunk);
    stopwatch_chunk.reset();

    spdlog::info("=======================================");
    spdlog::info("Map loader finished after {:.3} seconds", stopwatch_total);

    done = true;
}

MapLoader::~MapLoader() {
    if (_thread.joinable())
        _thread.join();
}

void MapLoader::init() {
    _thread = std::thread{ &MapLoader::load, this };
}

bool MapLoader::isDone() {
    return done;
}

void MapLoader::onDone() {
    _onLoadCallback(std::move(_map));
}

} // namespace geck
