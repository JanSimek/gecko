#include "MapLoader.h"
#include <thread>
#include <QString>
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
#include "../../util/QtDialogs.h"

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

    // Validate required LST files exist before loading
    const std::vector<std::string> requiredLstFiles = {
        "art/items/items.lst", 
        "art/critters/critters.lst", 
        "art/scenery/scenery.lst", 
        "art/walls/walls.lst", 
        "art/tiles/tiles.lst", 
        "art/misc/misc.lst", 
        "art/intrface/intrface.lst", 
        "art/inven/inven.lst"
    };
    
    std::vector<std::string> missingFiles;
    auto& resourceManager = ResourceManager::getInstance();
    
    for (const auto& lstPath : requiredLstFiles) {
        if (!resourceManager.fileExistsInVFS(lstPath)) {
            missingFiles.push_back(lstPath);
        }
    }
    
    if (!missingFiles.empty()) {
        std::string errorMessage = "Cannot load map: Missing required LST files:\n\n";
        for (const auto& missingFile : missingFiles) {
            errorMessage += "• " + missingFile + "\n";
        }
        errorMessage += "\nPlease ensure all Fallout 2 game files are properly installed and DAT archives are loaded.";
        
        _errorMessage = errorMessage;
        _hasError = true;
        done = true;  // Mark as done so LoadingWidget will call onDone()
        spdlog::error("Map loading failed: {} LST files missing", missingFiles.size());
        return;
    }

    // TODO: move to a new loader that is called only once per application start
    try {
        for (const auto& lst_path : requiredLstFiles) {
            ResourceManager::getInstance().loadResource<Lst>(lst_path);
        }
    } catch (const std::exception& e) {
        _errorMessage = QString("Failed to load required resource files:\n%1\n\nPlease ensure all game data files are properly configured.")
            .arg(e.what()).toStdString();
        _hasError = true;
        done = true;
        spdlog::error("Failed to load LST files: {}", e.what());
        return;
    }

    setProgress("Parsing map file");

    // MapReader requires callback in constructor, so we need to create it directly
    try {
        auto proLoadCallback = [&](uint32_t PID) {
            return ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(PID));
        };
        MapReader map_reader{ proLoadCallback };
        _map = map_reader.openFile(_mapPath);
    } catch (const std::exception& e) {
        _errorMessage = QString("Failed to parse map file:\n%1\n\nPlease ensure all game data files are properly configured.")
            .arg(e.what()).toStdString();
        _hasError = true;
        done = true;
        spdlog::error("Failed to parse map: {}", e.what());
        return;
    }

    if (_elevation == INVALID_ELEVATION) {
        uint32_t default_elevation = _map->getMapFile().header.player_default_elevation;
        spdlog::info("Using default map elevation {}", default_elevation);
        _elevation = default_elevation;
    }

    spdlog::info("... map file parsed in {:.3} seconds", stopwatch_chunk);
    stopwatch_chunk.reset();

    // Tiles
    Lst* lst = nullptr;
    try {
        lst = ResourceManager::getInstance().loadResource<Lst>("art/tiles/tiles.lst");
    } catch (const std::exception& e) {
        _errorMessage = QString("Failed to load tiles list file:\n%1\n\nPlease ensure all game data files are properly configured.")
            .arg(e.what()).toStdString();
        _hasError = true;
        done = true;
        spdlog::error("Failed to load tiles.lst: {}", e.what());
        return;
    }

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
    ResourceManager::getInstance().insertTexture("art/misc/light.frm");
    ResourceManager::getInstance().insertTexture("art/misc/wallblock.frm");
    ResourceManager::getInstance().insertTexture("art/misc/wallblockF.frm");
    ResourceManager::getInstance().insertTexture("art/misc/scrblk.frm");

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
    if (_hasError) {
        // Show error dialog on main thread
        QtDialogs::showError(nullptr, "Missing Game Files", QString::fromStdString(_errorMessage));
        // Call callback with null map to indicate failure
        _onLoadCallback(nullptr);
    } else {
        _onLoadCallback(std::move(_map));
    }
}

} // namespace geck
