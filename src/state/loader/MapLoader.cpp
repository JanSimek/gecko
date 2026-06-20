#include "MapLoader.h"
#include <string>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include "util/Constants.h"
#include "resource/ResourcePaths.h"

#include "reader/ReaderExceptions.h"
#include "reader/ReaderFactory.h"
#include "reader/map/MapReader.h"

#include "format/pro/Pro.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/lst/Lst.h"
#include "format/frm/Direction.h"

#include "resource/GameResources.h"
#include "resource/ResourceInitializer.h"

namespace geck {

MapLoader::MapLoader(std::shared_ptr<resource::GameResources> resources,
    const std::filesystem::path& mapFile,
    int elevation,
    bool forceFilesystem,
    std::function<void(std::unique_ptr<Map>)> onLoadCallback)
    : _resources(std::move(resources))
    , _mapPath(mapFile)
    , _elevation(elevation)
    , _forceFilesystem(forceFilesystem)
    , _onLoadCallback(onLoadCallback) {
}

void MapLoader::load() {
    if (_mapPath.empty()) {
        spdlog::error("MapLoader: No map path provided");
        _errorMessage = "No map path provided";
        _hasError = true;
        done = true;
        return;
    }

    setStatus("Loading map " + _mapPath.filename().string());
    _percentDone = 0;

    // Runs on a background thread (see the std::thread in start()): an exception escaping here
    // would call std::terminate and abort the app, so a failed/corrupt read must become a
    // reported error instead.
    try {
        if (_forceFilesystem) {
            spdlog::info("MapLoader: Force filesystem loading requested");
            loadFromFilesystem();
        } else {
            spdlog::info("MapLoader: Attempting VFS loading first");
            loadFromVFS();
        }
    } catch (const FileReaderException& e) {
        spdlog::error("MapLoader: failed to load '{}': {}", _mapPath.string(), e.what());
        _errorMessage = std::string("Failed to load map: ") + e.what();
        _hasError = true;
        done = true;
    }
}

void MapLoader::loadFromVFS() {
    spdlog::info("MapLoader: Loading map from VFS: {}", _mapPath.string());

    try {
        if (!ensureResourceListsReady()) {
            return;
        }

        setProgress("Parsing map file from VFS");
        _percentDone = 15;

        auto mapData = _resources->files().readRawBytes(_mapPath);
        if (!mapData) {
            spdlog::error("MapLoader: Failed to open map file in VFS: {}", _mapPath.string());
            _errorMessage = "Failed to open map file in VFS: " + _mapPath.string();
            _hasError = true;
            done = true;
            return;
        }

        auto proLoadCallback = [&](uint32_t PID) {
            return _resources->loadPro(PID);
        };
        MapReader mapReader(proLoadCallback);

        _map = mapReader.openFile(_mapPath.string(), *mapData);

        if (!_map) {
            spdlog::error("MapLoader: Failed to parse map data from VFS: {}", _mapPath.string());
            _errorMessage = "Failed to parse map file: " + _mapPath.string();
            _hasError = true;
            done = true;
            return;
        }

        _percentDone = 20;
        spdlog::info("MapLoader: Successfully loaded map from VFS: {}", _mapPath.string());

        loadMapResources();

        // Only mark complete if loadMapResources didn't set an error.
        if (!_hasError) {
            done = true;
        }

    } catch (const std::exception& e) {
        spdlog::error("MapLoader: Failed to load map from VFS: {}", e.what());
        _errorMessage = "Failed to load map file:\n" + _mapPath.string() + "\n\nError: " + e.what();
        _hasError = true;
        done = true;
    }
}

void MapLoader::loadFromFilesystem() {
    spdlog::info("MapLoader: Loading map from filesystem: {}", _mapPath.string());
    spdlog::stopwatch stopwatch_total;

    if (!ensureResourceListsReady()) {
        return;
    }

    setProgress("Parsing map file from filesystem");
    _percentDone = 15;

    // MapReader requires callback in constructor, so we need to create it directly
    try {
        auto proLoadCallback = [&](uint32_t PID) {
            return _resources->loadPro(PID);
        };
        MapReader map_reader{ proLoadCallback };
        _map = map_reader.openFile(_mapPath);
    } catch (const std::exception& e) {
        _errorMessage = "Failed to parse map file:\n" + std::string(e.what())
            + "\n\nPlease ensure all game data files are properly configured.";
        _hasError = true;
        done = true;
        spdlog::error("Failed to parse map: {}", e.what());
        return;
    }

    _percentDone = 20;
    spdlog::info("MapLoader: Successfully loaded map from filesystem: {}", _mapPath.string());

    loadMapResources();

    spdlog::info("Map loader finished after {:.3} seconds", stopwatch_total);

    // Only mark complete if loadMapResources didn't set an error.
    if (!_hasError) {
        done = true;
    }
}

bool MapLoader::ensureResourceListsReady() {
    try {
        setProgress("Checking resource lists");
        _percentDone = 10;
        ResourceInitializer::requireEssentialLstFilesLoaded(*_resources);
        return true;
    } catch (const std::exception& e) {
        _errorMessage = "Required game resource files are not initialized:\n" + std::string(e.what())
            + "\n\nPlease ensure the data paths are configured and loaded before opening a map.";
        _hasError = true;
        done = true;
        spdlog::error("MapLoader: Required resource lists are not initialized: {}", e.what());
        return false;
    }
}

void MapLoader::loadMapResources() {
    spdlog::stopwatch stopwatch_chunk;

    try {
        if (_elevation == INVALID_ELEVATION) {
            uint32_t default_elevation = _map->getMapFile().header.player_default_elevation;
            spdlog::info("Using default map elevation {}", default_elevation);
            _elevation = default_elevation;
        }

        Lst* lst = nullptr;
        try {
            lst = _resources->repository().load<Lst>(ResourcePaths::Lst::TILES);
        } catch (const std::exception& e) {
            _errorMessage = "Failed to load tiles list file:\n" + std::string(e.what())
                + "\n\nPlease ensure all game data files are properly configured.";
            _hasError = true;
            done = true;
            spdlog::error("Failed to load tiles.lst: {}", e.what());
            return;
        }

        size_t tile_number = 1;
        size_t tiles_total = lst->list().size();

        // Progress for tiles: 20% to 60% (40% of total progress)
        for (const auto& tile : lst->list()) {
            setProgress("Loading map tile texture " + std::to_string(tile_number) + " of " + std::to_string(tiles_total));
            _resources->textures().preload("art/tiles/" + tile);

            // Progress: 20% base + (current/total * 40%)
            int tileProgress = static_cast<int>((tile_number * 40) / tiles_total);
            _percentDone = 20 + tileProgress;
            tile_number++;
        }

        spdlog::info("... tile textures loaded in {:.3} seconds", stopwatch_chunk);
        stopwatch_chunk.reset();

        size_t objectNumber = 1;
        size_t objectsTotal = _map->objects().at(_elevation).size();

        // Progress for objects: 60% to 95% (35% of total progress)
        for (const auto& object : _map->objects().at(_elevation)) {
            setProgress("Loading map object " + std::to_string(objectNumber) + " of " + std::to_string(objectsTotal));

            if (object->position == -1) {
                objectNumber++;
                continue; // object inside an inventory/container
            }

            const std::string frmName = _resources->frmResolver().resolve(object->frm_pid);
            _resources->textures().preload(frmName);

            // Progress: 60% base + (current/total * 35%)
            int objectProgress = static_cast<int>((objectNumber * 35) / std::max(objectsTotal, size_t(1)));
            _percentDone = 60 + objectProgress;
            objectNumber++;
        }

        // Load essential editor textures
        _resources->textures().preload(ResourcePaths::Frm::BLANK_TILE);
        _resources->textures().preload(ResourcePaths::Frm::LIGHT);
        _resources->textures().preload(ResourcePaths::Frm::WALL_BLOCK);
        _resources->textures().preload(ResourcePaths::Frm::WALL_BLOCK_FULL);
        _resources->textures().preload(ResourcePaths::Frm::SCROLL_BLOCKER);

        spdlog::info("... objects and resources loaded in {:.3} seconds", stopwatch_chunk);

        _percentDone = 100;
        setProgress("Map loading complete");
    } catch (const std::exception& e) {
        spdlog::error("MapLoader: Failed to load map resources: {}", e.what());
        _errorMessage = "Failed to load map file:\n" + _mapPath.string() + "\n\nError: " + e.what();
        _hasError = true;
        done = true;
    }
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
        // Presentation is the caller's responsibility: onDone() runs on the main
        // thread via the load callback, which can inspect hasError()/errorMessage().
        spdlog::warn("MapLoader completed with errors: {}", _errorMessage);
        _onLoadCallback(nullptr);
    } else {
        _onLoadCallback(std::move(_map));
    }
}

} // namespace geck
