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

#include "../../util/ProHelper.h"
#include "../../util/ResourceManager.h"
#include "../../util/PathUtils.h"
#include "../../util/QtDialogs.h"

#include <vfspp/VirtualFileSystem.hpp>

namespace geck {

MapLoader::MapLoader(const std::filesystem::path& mapFile, int elevation, bool forceFilesystem, std::function<void(std::unique_ptr<Map>)> onLoadCallback)
    : _mapPath(mapFile)
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
        auto& resourceManager = ResourceManager::getInstance();
        auto vfs = resourceManager.getVFS();
        
        if (!vfs) {
            spdlog::error("MapLoader: VFS not available for map loading");
            _errorMessage = "Virtual file system not available";
            _hasError = true;
            done = true;
            return;
        }
        
        // Validate and load required LST files (same as filesystem loading)
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
            done = true;
            spdlog::error("VFS map loading failed: {} LST files missing", missingFiles.size());
            return;
        }

        // Load required LST files into ResourceManager cache
        try {
            setProgress("Loading resource lists");
            _percentDone = 5;
            
            for (const auto& lst_path : requiredLstFiles) {
                resourceManager.loadResource<Lst>(lst_path);
            }
            
            _percentDone = 10;
        } catch (const std::exception& e) {
            _errorMessage = QString("Failed to load required resource files:\n%1\n\nPlease ensure all game data files are properly configured.")
                .arg(e.what()).toStdString();
            _hasError = true;
            done = true;
            spdlog::error("Failed to load LST files for VFS map: {}", e.what());
            return;
        }
        
        setProgress("Parsing map file from VFS");
        _percentDone = 15;
        
        // Let VFS handle path resolution - it knows which files are mounted
        vfspp::FileInfo vfsFileInfo = PathUtils::createNormalizedFileInfo(_mapPath);
        
        // Open file in VFS
        vfspp::IFilePtr vfsFile = vfs->OpenFile(vfsFileInfo, vfspp::IFile::FileMode::Read);
        if (!vfsFile) {
            spdlog::error("MapLoader: Failed to open map file in VFS: {}", _mapPath.string());
            _errorMessage = "Failed to open map file in VFS: " + _mapPath.string();
            _hasError = true;
            done = true;
            return;
        }
        
        // Read file data into memory
        size_t fileSize = vfsFile->Size();
        std::vector<uint8_t> buffer(fileSize);
        size_t bytesRead = vfsFile->Read(buffer.data(), fileSize);
        
        if (bytesRead != fileSize) {
            spdlog::error("MapLoader: Failed to read complete map file from VFS: {} (read {} of {} bytes)", 
                         _mapPath.string(), bytesRead, fileSize);
            _errorMessage = "Failed to read complete map file from VFS";
            _hasError = true;
            done = true;
            return;
        }
        
        // Create MapReader and load from memory buffer
        auto proLoadCallback = [&](uint32_t PID) {
            return resourceManager.loadResource<Pro>(ProHelper::basePath(PID));
        };
        MapReader mapReader(proLoadCallback);
        
        // Load map directly from data buffer
        _map = mapReader.openFile(_mapPath.string(), buffer);
        
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
        done = true;
        spdlog::error("Map loading failed: {} LST files missing", missingFiles.size());
        return;
    }

    // TODO: move to a new loader that is called only once per application start
    try {
        setProgress("Loading resource lists");
        _percentDone = 5;
        
        for (const auto& lst_path : requiredLstFiles) {
            ResourceManager::getInstance().loadResource<Lst>(lst_path);
        }
        
        _percentDone = 10;
    } catch (const std::exception& e) {
        _errorMessage = QString("Failed to load required resource files:\n%1\n\nPlease ensure all game data files are properly configured.")
            .arg(e.what()).toStdString();
        _hasError = true;
        done = true;
        spdlog::error("Failed to load LST files: {}", e.what());
        return;
    }

    setProgress("Parsing map file from filesystem");
    _percentDone = 15;

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

    _percentDone = 20;
    spdlog::info("MapLoader: Successfully loaded map from filesystem: {}", _mapPath.string());
    
    // Load additional resources (textures, etc.)
    loadMapResources();
    
    spdlog::info("Map loader finished after {:.3} seconds", stopwatch_total);
    done = true;
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
        lst = ResourceManager::getInstance().loadResource<Lst>("art/tiles/tiles.lst");
    } catch (const std::exception& e) {
        _errorMessage = QString("Failed to load tiles list file:\n%1\n\nPlease ensure all game data files are properly configured.")
            .arg(e.what()).toStdString();
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
        ResourceManager::getInstance().insertTexture("art/tiles/" + tile);
        
        // Calculate progress: 20% base + (current/total * 40%)
        int tileProgress = (tile_number * 40) / tiles_total;
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

        const std::string frmName = ResourceManager::getInstance().FIDtoFrmName(object->frm_pid);
        ResourceManager::getInstance().insertTexture(frmName);
        
        // Calculate progress: 60% base + (current/total * 35%)
        int objectProgress = (objectNumber * 35) / std::max(objectsTotal, size_t(1));
        _percentDone = 60 + objectProgress;
        objectNumber++;
    }

    // Load essential editor textures
    ResourceManager::getInstance().insertTexture("art/tiles/blank.frm");
    ResourceManager::getInstance().insertTexture("art/misc/light.frm");
    ResourceManager::getInstance().insertTexture("art/misc/wallblock.frm");
    ResourceManager::getInstance().insertTexture("art/misc/wallblockF.frm");
    ResourceManager::getInstance().insertTexture("art/misc/scrblk.frm");

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
