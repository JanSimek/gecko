#include "ResourceInitializer.h"
#include "ResourceManager.h"
#include "../format/lst/Lst.h"
#include <spdlog/spdlog.h>

namespace geck {

void ResourceInitializer::loadEssentialLstFiles() {
    auto& resourceManager = ResourceManager::getInstance();
    
    try {
        // Load all LST files needed for FID to FRM name resolution
        // These correspond to the frmTypeDescription array in ResourceManager::FIDtoFrmName
        resourceManager.loadResource<Lst>("art/items/items.lst");
        resourceManager.loadResource<Lst>("art/critters/critters.lst");
        resourceManager.loadResource<Lst>("art/scenery/scenery.lst");
        resourceManager.loadResource<Lst>("art/walls/walls.lst");
        resourceManager.loadResource<Lst>("art/tiles/tiles.lst");
        resourceManager.loadResource<Lst>("art/misc/misc.lst");
        resourceManager.loadResource<Lst>("art/intrface/intrface.lst");
        resourceManager.loadResource<Lst>("art/inven/inven.lst");
        
        spdlog::info("ResourceInitializer: Loaded all essential LST files");
    } catch (const std::exception& e) {
        spdlog::error("ResourceInitializer: Failed to load essential LST files: {}", e.what());
        throw;
    }
}

void ResourceInitializer::loadEssentialTextures() {
    auto& resourceManager = ResourceManager::getInstance();
    
    try {
        // Load essential textures for map display and object visualization
        resourceManager.insertTexture("art/tiles/blank.frm");
        resourceManager.insertTexture("art/misc/scrblk.frm");
        resourceManager.insertTexture("art/misc/wallblock.frm");
        resourceManager.insertTexture("art/misc/wallblockF.frm");
        
        spdlog::info("ResourceInitializer: Loaded all essential textures");
    } catch (const std::exception& e) {
        spdlog::error("ResourceInitializer: Failed to load essential textures: {}", e.what());
        throw;
    }
}

void ResourceInitializer::loadEssentialResources() {
    try {
        loadEssentialTextures();
        loadEssentialLstFiles();
        
        spdlog::info("ResourceInitializer: Loaded all essential resources");
    } catch (const std::exception& e) {
        spdlog::error("ResourceInitializer: Failed to load essential resources: {}", e.what());
        throw;
    }
}

} // namespace geck