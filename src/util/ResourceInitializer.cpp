#include "ResourceInitializer.h"
#include "resource/GameResources.h"
#include "ResourcePaths.h"
#include "../format/lst/Lst.h"

#include <array>
#include <vector>
#include <sstream>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <stdexcept>

namespace geck {
namespace {

constexpr std::array<std::string_view, 8> EssentialLstFiles = {
    ResourcePaths::Lst::ITEMS,
    ResourcePaths::Lst::CRITTERS,
    ResourcePaths::Lst::SCENERY,
    ResourcePaths::Lst::WALLS,
    ResourcePaths::Lst::TILES,
    ResourcePaths::Lst::MISC,
    ResourcePaths::Lst::INTERFACE,
    ResourcePaths::Lst::INVENTORY,
};

std::runtime_error makeMissingLstError(const std::vector<std::string>& missingFiles) {
    std::ostringstream message;
    message << "Missing required LST files:\n";
    for (const auto& missingFile : missingFiles) {
        message << "- " << missingFile << '\n';
    }
    return std::runtime_error(message.str());
}

} // namespace

void ResourceInitializer::loadEssentialLstFiles(resource::GameResources& resources) {
    try {
        std::vector<std::string> missingFiles;
        missingFiles.reserve(EssentialLstFiles.size());

        for (const auto& path : EssentialLstFiles) {
            if (!resources.files().exists(std::filesystem::path(path))) {
                missingFiles.emplace_back(path);
            }
        }

        if (!missingFiles.empty()) {
            throw makeMissingLstError(missingFiles);
        }

        const auto loadLst = [&](std::string_view path) {
            [[maybe_unused]] auto* lst = resources.repository().load<Lst>(path);
            if (!lst) {
                throw std::runtime_error("Failed to load essential LST: " + std::string(path));
            }
        };

        for (const auto& path : EssentialLstFiles) {
            loadLst(path);
        }

        spdlog::info("ResourceInitializer: Loaded all essential LST files");
    } catch (const std::exception& e) {
        spdlog::error("ResourceInitializer: Failed to load essential LST files: {}", e.what());
        throw;
    }
}

void ResourceInitializer::requireEssentialLstFilesLoaded(resource::GameResources& resources) {
    std::vector<std::string> missingFiles;
    missingFiles.reserve(EssentialLstFiles.size());

    for (const auto& path : EssentialLstFiles) {
        if (!resources.repository().find<Lst>(path)) {
            missingFiles.emplace_back(path);
        }
    }

    if (!missingFiles.empty()) {
        throw makeMissingLstError(missingFiles);
    }
}

void ResourceInitializer::loadEssentialTextures(resource::GameResources& resources) {
    try {
        resources.textures().preload(ResourcePaths::Frm::BLANK_TILE);
        resources.textures().preload(ResourcePaths::Frm::LIGHT);
        resources.textures().preload(ResourcePaths::Frm::SCROLL_BLOCKER);
        resources.textures().preload(ResourcePaths::Frm::WALL_BLOCK);
        resources.textures().preload(ResourcePaths::Frm::WALL_BLOCK_FULL);
        resources.textures().preload(ResourcePaths::Frm::EXIT_GRID);

        spdlog::info("ResourceInitializer: Loaded all essential textures");
    } catch (const std::exception& e) {
        spdlog::error("ResourceInitializer: Failed to load essential textures: {}", e.what());
        throw;
    }
}

} // namespace geck
