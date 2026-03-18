#include "MapLoader.h"
#include <thread>
#include <QString>
#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include "../../util/Constants.h"
#include "../../util/ResourcePaths.h"

#include "../../reader/ReaderFactory.h"
#include "../../reader/map/MapReader.h"

#include "../../format/pro/Pro.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapObject.h"
#include "../../format/map/Tile.h"
#include "../../format/lst/Lst.h"
#include "../../format/frm/Direction.h"

#include "../../resource/GameResources.h"
#include "../../util/ProHelper.h"
#include "../../util/QtDialogs.h"
#include "../../util/ResourceInitializer.h"

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

    // Dispatch to appropriate loading method based on source context
    if (_forceFilesystem) {
        spdlog::info("MapLoader: Force filesystem loading requested");
        loadFromFilesystem();
    } else {
        spdlog::info("MapLoader: Attempting VFS loading first");
        loadFromVFS();
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

        // Create MapReader and load from memory buffer
        auto proLoadCallback = [&](uint32_t PID) {
            return _resources->repository().load<Pro>(ProHelper::basePath(*_resources, PID));
        };
        MapReader mapReader(proLoadCallback);

        // Load map directly from data buffer
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

        // Load additional resources (textures, etc.) - reuse existing code
        loadMapResources();

        // Mark loading as complete (only if loadMapResources didn't set error)
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
            return _resources->repository().load<Pro>(ProHelper::basePath(*_resources, PID));
        };
        MapReader map_reader{ proLoadCallback };
        _map = map_reader.openFile(_mapPath);
    } catch (const std::exception& e) {
        _errorMessage = QString("Failed to parse map file:\n%1\n\nPlease ensure all game data files are properly configured.")
                            .arg(e.what())
                            .toStdString();
        _hasError = true;
        done = true;
        spdlog::error("Failed to parse map: {}", e.what());
        return;
    }

    _percentDone = 20;
    spdlog::info("MapLoader: Successfully loaded map from filesystem: {}", _mapPath.string());

    // Load additional resources (textures, etc.)
    loadMapResources();

    spdlog::info("Map loader finished after {:.3} seconds", stopwatch_total);
    done = true;
}

bool MapLoader::ensureResourceListsReady() {
    try {
        setProgress("Checking resource lists");
        _percentDone = 10;
        ResourceInitializer::requireEssentialLstFilesLoaded(*_resources);
        return true;
    } catch (const std::exception& e) {
        _errorMessage = QString("Required game resource files are not initialized:\n%1\n\nPlease ensure the data paths are configured and loaded before opening a map.")
                            .arg(e.what())
                            .toStdString();
        _hasError = true;
        done = true;
        spdlog::error("MapLoader: Required resource lists are not initialized: {}", e.what());
        return false;
    }
}

void MapLoader::loadMapResources() {
    spdlog::stopwatch stopwatch_chunk;

    if (_elevation == INVALID_ELEVATION) {
        uint32_t default_elevation = _map->getMapFile().header.player_default_elevation;
        spdlog::info("Using default map elevation {}", default_elevation);
        _elevation = default_elevation;
    }

    // Load tile textures
    Lst* lst = nullptr;
    try {
        lst = _resources->repository().load<Lst>(ResourcePaths::Lst::TILES);
    } catch (const std::exception& e) {
        _errorMessage = QString("Failed to load tiles list file:\n%1\n\nPlease ensure all game data files are properly configured.")
                            .arg(e.what())
                            .toStdString();
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

        // Calculate progress: 20% base + (current/total * 40%)
        int tileProgress = static_cast<int>((tile_number * 40) / tiles_total);
        _percentDone = 20 + tileProgress;
        tile_number++;
    }

    spdlog::info("... tile textures loaded in {:.3} seconds", stopwatch_chunk);
    stopwatch_chunk.reset();

    // Load object textures
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

        // Calculate progress: 60% base + (current/total * 35%)
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

    // Mark as complete
    _percentDone = 100;
    setProgress("Map loading complete");
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
