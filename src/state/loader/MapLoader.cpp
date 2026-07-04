#include "MapLoader.h"
#include <algorithm>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
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

namespace {
    // Join up to `cap` names for a single log line, appending "… (+N more)" when truncated, so a
    // warning stays readable even when a whole data set's worth of art is unresolved.
    std::string joinCapped(const std::vector<std::string>& names, std::size_t cap) {
        std::string out;
        const std::size_t shown = std::min(names.size(), cap);
        for (std::size_t i = 0; i < shown; ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += names[i];
        }
        if (names.size() > cap) {
            out += " … (+" + std::to_string(names.size() - cap) + " more)";
        }
        return out;
    }

    // How many names to spell out in a single warning before collapsing the rest into a count.
    constexpr std::size_t kMaxNamesInWarning = 40;
} // namespace

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
            spdlog::debug("MapLoader: Force filesystem loading requested");
            loadFromFilesystem();
        } else {
            spdlog::debug("MapLoader: Attempting VFS loading first");
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
    spdlog::debug("MapLoader: Loading map from VFS: {}", _mapPath.string());

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
        spdlog::debug("MapLoader: Successfully loaded map from VFS: {}", _mapPath.string());

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
    spdlog::debug("MapLoader: Loading map from filesystem: {}", _mapPath.string());
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
    spdlog::debug("MapLoader: Successfully loaded map from filesystem: {}", _mapPath.string());

    loadMapResources();

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
            spdlog::debug("Using default map elevation {}", default_elevation);
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
        std::vector<std::string> unresolvedTiles;
        for (const auto& tile : lst->list()) {
            setProgress("Loading map tile texture " + std::to_string(tile_number) + " of " + std::to_string(tiles_total));
            try {
                _resources->textures().preload("art/tiles/" + tile);
            } catch (const std::exception& e) {
                // A tiles.lst entry whose art isn't in the mounted data is NOT fatal. The engine
                // loads tiles on demand, so a full tiles.lst that indexes art a given data set does
                // not ship (common with mods that patch tiles.lst but rely on master.dat for the
                // rest, or carry unused/placeholder slots) is normal. Skip it: the tile renders
                // blank only if a map actually places it. Aborting here would make an otherwise
                // loadable map fail on a tile it never uses.
                unresolvedTiles.push_back(tile);
                spdlog::debug("MapLoader: skipping unresolved tile '{}': {}", tile, e.what());
            }

            // Progress: 20% base + (current/total * 40%)
            int tileProgress = static_cast<int>((tile_number * 40) / tiles_total);
            _percentDone = 20 + tileProgress;
            tile_number++;
        }
        if (!unresolvedTiles.empty()) {
            spdlog::warn("MapLoader: {} of {} tiles.lst entries could not be resolved in the mounted "
                         "data; they render blank only where a map places them: {}",
                unresolvedTiles.size(), tiles_total, joinCapped(unresolvedTiles, kMaxNamesInWarning));
        }

        stopwatch_chunk.reset();

        size_t objectNumber = 1;
        size_t objectsTotal = _map->objects().at(_elevation).size();

        // Progress for objects: 60% to 95% (35% of total progress)
        std::vector<std::string> unresolvedObjectArt;
        std::unordered_set<std::string> seenUnresolvedArt; // dedupe: many objects can share one sprite
        for (const auto& object : _map->objects().at(_elevation)) {
            setProgress("Loading map object " + std::to_string(objectNumber) + " of " + std::to_string(objectsTotal));

            if (object->position == -1) {
                objectNumber++;
                continue; // object inside an inventory/container
            }

            // Same tolerance as tiles: a single object whose art (or proto->FID mapping) can't be
            // resolved must not abort the whole map. resolve() itself throws on an out-of-range FID,
            // so both it and preload() sit inside the guard; the object just renders blank. `art`
            // stays empty when resolve() is what threw, so we fall back to naming the FID.
            std::string art;
            try {
                art = _resources->frmResolver().resolve(object->frm_pid);
                _resources->textures().preload(art);
            } catch (const std::exception& e) {
                const std::string name = art.empty() ? ("fid " + std::to_string(object->frm_pid)) : art;
                if (seenUnresolvedArt.insert(name).second) {
                    unresolvedObjectArt.push_back(name);
                }
                spdlog::debug("MapLoader: skipping unresolved art for object fid {}: {}",
                    object->frm_pid, e.what());
            }

            // Progress: 60% base + (current/total * 35%)
            int objectProgress = static_cast<int>((objectNumber * 35) / std::max(objectsTotal, size_t(1)));
            _percentDone = 60 + objectProgress;
            objectNumber++;
        }
        if (!unresolvedObjectArt.empty()) {
            spdlog::warn("MapLoader: {} distinct object sprite(s) on elevation {} could not be resolved "
                         "in the mounted data; those objects render blank: {}",
                unresolvedObjectArt.size(), _elevation, joinCapped(unresolvedObjectArt, kMaxNamesInWarning));
        }

        // Load essential editor textures
        _resources->textures().preload(ResourcePaths::Frm::BLANK_TILE);
        _resources->textures().preload(ResourcePaths::Frm::LIGHT);
        _resources->textures().preload(ResourcePaths::Frm::WALL_BLOCK);
        _resources->textures().preload(ResourcePaths::Frm::WALL_BLOCK_FULL);
        _resources->textures().preload(ResourcePaths::Frm::SCROLL_BLOCKER);

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
