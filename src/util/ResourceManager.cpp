#include "ResourceManager.h"

// Prevent Windows API macros from interfering with method names
#ifdef _WIN32
#ifdef CreateFile
#undef CreateFile
#endif
#ifdef CopyFile
#undef CopyFile
#endif
#endif

#include "Constants.h"
#include "Exceptions.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <regex>
#include <spdlog/spdlog.h>

#include <vfspp/NativeFileSystem.hpp>
#include "vfs/Dat2FileSystem.hpp"

#include "reader/ReaderFactory.h"

#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/lst/Lst.h"
#include "format/pro/Pro.h"

namespace geck {

ResourceManager::ResourceManager()
    : _vfs(std::make_shared<vfspp::VirtualFileSystem>()) {
}

void ResourceManager::cleanup() {
    spdlog::info("Cleaning up ResourceManager resources...");

    // Clear textures first (they are OpenGL resources)
    _textures.clear();

    // Clear other resources
    _resources.clear();

    spdlog::info("ResourceManager cleanup completed");
}

void ResourceManager::storeTexture(std::string_view name, std::unique_ptr<sf::Texture> texture) {
    _textures.try_emplace(std::string{name}, std::move(texture));
}

bool ResourceManager::exists(std::string_view filename) const {
    const std::string filenameStr{filename};
    return _textures.contains(filenameStr) || _resources.contains(filenameStr);
}

bool ResourceManager::fileExistsInVFS(const std::filesystem::path& filepath) const {
    if (!_vfs) {
        return false;
    }
    
    // vfspp needs leading slash for root-relative paths
    std::filesystem::path vfsPath = "/" / filepath;
    vfspp::FileInfo fileInfo = PathUtils::createNormalizedFileInfo(vfsPath);
    
    // Try to open file in read mode to check if it exists
    vfspp::IFilePtr file = _vfs->OpenFile(fileInfo, vfspp::IFile::FileMode::Read);
    return file != nullptr;
}

void ResourceManager::insertTexture(std::string_view filename) {
    const std::string filenameStr{filename};
    // Check if texture is already in cache
    if (_textures.contains(filenameStr)) {
        return;
    }

    // lowercase file extension
    std::string extension = [&]() {
        std::string s = std::filesystem::path(filenameStr).extension().string();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }();

    if (extension.rfind(".frm", 0) == 0) { // frm, frm0, frmX..
        [[maybe_unused]] auto frm = loadResource<Frm>(filenameStr);
    } else {
        auto texture = std::make_unique<sf::Texture>();
        if (!texture->loadFromFile(filenameStr)) { // default to SFML's implementation
            throw ResourceException("Failed to load texture with extension: " + extension, filenameStr);
        }
        _textures.try_emplace(filenameStr, std::move(texture));
    }
}

const sf::Texture& ResourceManager::texture(std::string_view filename) {
    const std::string filenameStr{filename};
    const auto found = _textures.find(filenameStr);
    
    if (found != _textures.end()) {
        return *found->second;
    }

    auto frm = getResource<Frm>(filenameStr); // TODO: check extension?

    if (frm == nullptr) {
        // Try on-demand loading if FRM resource not found
        try {
            insertTexture(filename);
            
            frm = getResource<Frm>(filenameStr);
            if (frm == nullptr) {
                // Check if texture was created directly (for non-FRM files)
                const auto foundAfterLoad = _textures.find(filenameStr);
                if (foundAfterLoad != _textures.end()) {
                    return *foundAfterLoad->second;
                }
                // If still null, this file doesn't exist
                throw ResourceException("Texture does not exist", filenameStr);
            }
        } catch (const std::exception& e) {
                spdlog::error("ResourceManager: On-demand texture loading failed for {}: {}", filename, e.what());
                throw ResourceException("Texture loading failed", filenameStr);
            }
        }

        auto texture = std::make_unique<sf::Texture>();
        
        // Load palette for FRM texture creation
        auto pal = loadResource<Pal>("color.pal"); // TODO: custom pal
        if (!pal) {
            throw ResourceException("Failed to load color palette for texture", filenameStr);
        }

        // Create image from FRM and load it into texture
        try {
            const sf::Image image = imageFromFrm(frm, pal);
            if (!texture->loadFromImage(image)) {
                throw SpriteException("Failed to load texture from FRM image", filenameStr);
            }
        } catch (const std::exception& e) {
            throw SpriteException("Failed to create texture from FRM: " + std::string(e.what()), filenameStr);
        }

        // Store texture in cache using try_emplace
        const auto [iter, inserted] = _textures.try_emplace(filenameStr, std::move(texture));
        if (inserted) {
            return *iter->second;
        } else {
            throw ResourceException("Failed to cache texture", filenameStr);
        }
}

void ResourceManager::addDataPath(const std::filesystem::path& path) {
    vfspp::IFileSystemPtr vfsPtr;

    if (std::filesystem::is_directory(path)) {
        vfsPtr = std::make_shared<vfspp::NativeFileSystem>(path.string());

        std::filesystem::path masterDat = path / "master.dat";
        if (std::filesystem::exists(masterDat) && std::filesystem::is_regular_file(masterDat)) {
            addDataPath(masterDat);
        }

        std::filesystem::path critterDat = path / "critter.dat";
        if (std::filesystem::exists(critterDat) && std::filesystem::is_regular_file(critterDat)) {
            addDataPath(critterDat);
        }
    } else if (path.extension() == ".dat") {
        vfsPtr = std::shared_ptr<geck::GeckDat2FileSystem>(new geck::GeckDat2FileSystem(path.string()));
    } else {
        spdlog::error("Unsupported data location: {}", path.string());
        return;
    }

    vfsPtr->Initialize();
    if (!vfsPtr->IsInitialized()) {
        spdlog::error("Failed to initialize GeckDat2FileSystem: {}", path.string());
        return;
    }

    _vfs->AddFileSystem("/", vfsPtr);
    spdlog::info("Location '{}' was added to the data path", path.string());
}

template <class T, typename Key>
T* ResourceManager::getResource(const Key& id) {

    static_assert(std::is_base_of<IFile, T>::value, "T must derive from IFile");

    auto itemIt = _resources.find(id);
    if (itemIt != _resources.end()) {
        auto resource = dynamic_cast<T*>(itemIt->second.get());
        if (resource == nullptr) {
            throw std::runtime_error{ "Requested file type does not match type in the cache: " + id };
        }
        return resource;
    }

    // TODO: load on-demand
    spdlog::error("Resource {} not found in resource cache", id);

    return nullptr;
}

const sf::Image ResourceManager::imageFromFrm(Frm* frm, Pal* pal) {

    spdlog::debug("Stitching {} texture from {} directions", frm->filename(), frm->directions().size());

    auto colors = pal->palette();

    // find maximum width and height
    unsigned maxWidth = frm->maxFrameWidth();
    unsigned maxHeight = frm->maxFrameHeight();

    sf::Image image{};
    image.resize({ frm->width(), frm->height() }, { 0, 0, 0, 0 });

    int yOffset = 0;
    for (const auto& direction : frm->directions()) {
        int xOffset = 0;

        for (const Frame& frame : direction.frames()) {

            for (int x = 0; x < frame.width(); x++) {
                for (int y = 0; y < frame.height(); y++) {

                    uint8_t paletteIndex = frame.index(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
                    geck::Rgb color = colors[paletteIndex];

                    constexpr uint8_t white = 255;
                    constexpr uint8_t opaque_alpha = 255;

                    uint8_t r, g, b, a;
                    if (color.r == white && color.g == white && color.b == white) {
                        // transparent
                        r = 0;
                        g = 0;
                        b = 0;
                        a = 0;
                    } else {
                        constexpr int brightness = 4; // brightness modifier
                        r = color.r * brightness;
                        g = color.g * brightness;
                        b = color.b * brightness;
                        a = opaque_alpha;
                    }

                    image.setPixel(
                        { maxWidth * xOffset + x, maxHeight * yOffset + y },
                        { r, g, b, a });
                }
            }
            xOffset++;
        }
        yOffset++;
    }

    return image;
}
std::string ResourceManager::FIDtoFrmName(unsigned int FID) {

    /*const*/ auto baseId = FID & FileFormat::BASE_ID_MASK; 
    /*const*/ auto type = static_cast<Frm::FRM_TYPE>(FID >> FileFormat::TYPE_MASK_SHIFT);
    
    spdlog::debug("ResourceManager: FIDtoFrmName called with FID=0x{:08X}, type={}, baseId={}", 
                  FID, static_cast<int>(type), baseId);

    if (type == Frm::FRM_TYPE::CRITTER) {
        baseId = FID & FileFormat::CRITTER_ID_MASK;
        type = static_cast<Frm::FRM_TYPE>((FID & FileFormat::TYPE_MASK) >> FileFormat::TYPE_MASK_SHIFT);
    }

    if (type == Frm::FRM_TYPE::MISC && baseId == WallBlockers::SCROLL_BLOCKER_BASE_ID) {
        static const std::string SCROLL_BLOCKERS_PATH("art/misc/scrblk.frm");
        // Map scroll blockers
        return SCROLL_BLOCKERS_PATH;
    }

    if (type == Frm::FRM_TYPE::WALL && baseId == 620) {
        static const std::string SCROLL_BLOCKERS_PATH("art/misc/wallblock.frm");
        // Map scroll blockers
        return SCROLL_BLOCKERS_PATH;
    }

    // FIXME: Light source object (proto/scenery/00000142.pro) uses FID=0x02000015, so baseId=21 which currently points to block.frm
    /*
    if (type == Frm::FRM_TYPE::SCENERY && FID == 0x02000015) {
        static const std::string LIGHT_PATH("art/misc/light.frm");
        spdlog::info("ResourceManager: FIDtoFrmName detected light source scenery baseId 21, returning light.frm (FID=0x{:08X}), FID={}", FID, FID);
        return LIGHT_PATH;
    }
    */

    if (type > Frm::FRM_TYPE::INVENTORY) {
        throw std::runtime_error{ "Invalid FRM_TYPE" };
    }

    // TODO: art/$/
    static struct TypeArtListDecription {
        const std::string prefixPath;
        const std::string lstFilePath;
    } const frmTypeDescription[] = {
        { "art/items/", "art/items/items.lst" },
        { "art/critters/", "art/critters/critters.lst" },
        { "art/scenery/", "art/scenery/scenery.lst" },
        { "art/walls/", "art/walls/walls.lst" },
        { "art/tiles/", "art/tiles/tiles.lst" },
        { "art/misc/", "art/misc/misc.lst" },
        { "art/intrface/", "art/intrface/intrface.lst" },
        { "art/inven/", "art/inven/inven.lst" },
    };

    const auto& typeArtDescription = frmTypeDescription[static_cast<size_t>(type)];

    const auto& lst = getResource<Lst>(typeArtDescription.lstFilePath);

    if (baseId >= lst->list().size()) {
        throw std::runtime_error{ "LST " + typeArtDescription.lstFilePath + " size " + std::to_string(lst->list().size()) + " <= frmID: " + std::to_string(baseId) + ", frmType: " + std::to_string(static_cast<unsigned>(type)) };
    }

    std::string frm_name = lst->list().at(baseId);

    if (type == Frm::FRM_TYPE::CRITTER) {
        return typeArtDescription.prefixPath + frm_name.substr(0, 6) + Frm::STANDING_ANIMATION_SUFFIX;
    }
    return typeArtDescription.prefixPath + frm_name;
}

// ========================================
// Extended VFS Methods (File Listing)
// ========================================

std::vector<std::string> ResourceManager::listAllFiles() const {
    return _vfs->ListAllFiles();
}

std::vector<std::string> ResourceManager::listFilesByPattern(const std::string& pattern) const {
    // For now, we'll implement pattern matching on top of ListAllFiles
    // This could be optimized later by adding pattern support to VFSPP
    auto allFiles = _vfs->ListAllFiles();
    std::vector<std::string> matchingFiles;

    // Simple wildcard pattern matching
    std::string regex_pattern = pattern;
    // Replace * with .*
    size_t pos = 0;
    while ((pos = regex_pattern.find('*', pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".*");
        pos += 2;
    }
    // Replace ? with .
    pos = 0;
    while ((pos = regex_pattern.find('?', pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".");
        pos += 1;
    }

    std::regex regex(regex_pattern);
    for (const auto& file : allFiles) {
        if (std::regex_match(file, regex)) {
            matchingFiles.push_back(file);
        }
    }

    return matchingFiles;
}

void ResourceManager::clearTextureCache(const std::string& filename) {
    auto it = _textures.find(filename);
    if (it != _textures.end()) {
        spdlog::debug("ResourceManager::clearTextureCache - removing cached texture for: {}", filename);
        _textures.erase(it);
    } else {
        spdlog::debug("ResourceManager::clearTextureCache - texture not found in cache: {}", filename);
    }
}

} // namespace geck
