#include "ResourceInitializer.h"
#include "ResourceManager.h"
#include "ResourcePaths.h"
#include "../format/lst/Lst.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <stdexcept>

namespace geck {

void ResourceInitializer::loadEssentialLstFiles() {
    auto& resourceManager = ResourceManager::getInstance();

    try {
        // Load all LST files needed for FID to FRM name resolution
        // These correspond to the frmTypeDescription array in ResourceManager::FIDtoFrmName
        const auto loadLst = [&](const std::filesystem::path& path) {
            [[maybe_unused]] auto* lst = resourceManager.loadResource<Lst>(path);
            if (!lst) {
                throw std::runtime_error("Failed to load essential LST: " + path.string());
            }
        };

        loadLst(ResourcePaths::Lst::ITEMS);
        loadLst(ResourcePaths::Lst::CRITTERS);
        loadLst(ResourcePaths::Lst::SCENERY);
        loadLst(ResourcePaths::Lst::WALLS);
        loadLst(ResourcePaths::Lst::TILES);
        loadLst(ResourcePaths::Lst::MISC);
        loadLst(ResourcePaths::Lst::INTERFACE);
        loadLst(ResourcePaths::Lst::INVENTORY);

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
        resourceManager.insertTexture(ResourcePaths::Frm::BLANK_TILE);
        resourceManager.insertTexture(ResourcePaths::Frm::LIGHT);
        resourceManager.insertTexture(ResourcePaths::Frm::SCROLL_BLOCKER);
        resourceManager.insertTexture(ResourcePaths::Frm::WALL_BLOCK);
        resourceManager.insertTexture(ResourcePaths::Frm::WALL_BLOCK_FULL);
        resourceManager.insertTexture(ResourcePaths::Frm::EXIT_GRID);

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
