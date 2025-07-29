#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <unordered_map>

#include <SFML/Graphics.hpp>

#include <vfspp/VirtualFileSystem.hpp>
#include "PathUtils.h"

#include "format/IFile.h"
#include "format/pal/Pal.h"
#include "format/frm/Frm.h"
#include "format/pro/Pro.h"
#include "format/msg/Msg.h"

#include "reader/FileParser.h"
#include "reader/dat/DatReader.h"
#include "reader/ReaderFactory.h"

namespace geck {

class ResourceManager {
private:
    vfspp::VirtualFileSystemPtr _vfs;

    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> _textures;
    std::unordered_map<std::string, std::unique_ptr<IFile>> _resources;

    ResourceManager(); // private constructor for singleton

    const sf::Image imageFromFrm(Frm* frm, Pal* pal);

public:
    // Singleton
    ResourceManager(ResourceManager const&) = delete;
    void operator=(ResourceManager const&) = delete;

    static ResourceManager& getInstance() {
        static ResourceManager instance; // Guaranteed to be destroyed.
        return instance;
    }

    template <class T, typename Key>
    [[nodiscard]] T* getResource(const Key& filepath);

    void cleanup();

    // Store a custom texture in the resource manager
    void storeTexture(std::string_view name, std::unique_ptr<sf::Texture> texture);

    template <class Resource>
    [[nodiscard]] Resource* loadResource(const std::filesystem::path& path) {
        static_assert(std::is_base_of<IFile, Resource>::value, "Resource must derive from IFile");

        const std::string pathKey = path.string();
        
        // Check if already loaded
        auto existingIter = _resources.find(pathKey);
        if (existingIter == _resources.end()) {
            try {
                // Load data from VFS
                const std::filesystem::path vfsPath = "/" / path;
                vfspp::IFilePtr file = _vfs->OpenFile(PathUtils::createNormalizedFileInfo(vfsPath), vfspp::IFile::FileMode::Read);

                if (!file || !file->IsOpened()) {
                    throw ResourceException("Failed to open file from VFS", vfsPath);
                }

                // Read file data from VFS into memory
                std::vector<uint8_t> data(file->Size());
                file->Read(data, file->Size());

                auto resource = ReaderFactory::readFileFromMemory<Resource>(data, pathKey);

                _resources[pathKey] = std::move(resource);
                
            } catch (const std::exception& e) {
                throw ResourceException("Failed to load resource: " + std::string(e.what()), path);
            }
        }

        return dynamic_cast<Resource*>(_resources.at(pathKey).get());
    }

    [[nodiscard]] bool exists(std::string_view filename) const;
    [[nodiscard]] bool fileExistsInVFS(const std::filesystem::path& filepath) const;
    void insertTexture(std::string_view filename);

    [[nodiscard]] const sf::Texture& texture(std::string_view filename);

    [[nodiscard]] std::string FIDtoFrmName(unsigned int FID);

    void addDataPath(const std::filesystem::path& path);

    // ========================================
    // Extended VFS Methods (File Listing)
    // ========================================

    /**
     * @brief List all files from all mounted filesystems
     * @return Vector of all file paths (absolute paths with aliases)
     */
    std::vector<std::string> listAllFiles() const;

    /**
     * @brief List files matching a glob pattern
     * @param pattern Glob pattern (e.g., "*.lst", "art/items/")
     * @return Vector of matching file paths
     */
    std::vector<std::string> listFilesByPattern(const std::string& pattern) const;
    
    /**
     * @brief Get access to the virtual file system
     * @return Shared pointer to the VFS
     */
    vfspp::VirtualFileSystemPtr getVFS() const { return _vfs; }
    
    // Cache management methods
    void clearTextureCache(const std::string& filename);
};

} // namespace geck
