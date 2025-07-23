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
    T* getResource(const Key& filepath);

    void cleanup();

    // Store a custom texture in the resource manager
    void storeTexture(const std::string& name, std::unique_ptr<sf::Texture> texture);

    template <class Resource>
    Resource* loadResource(const std::filesystem::path& path) {
        static_assert(std::is_base_of<IFile, Resource>::value, "Resource must derive from IFile");

        if (!exists(path.string())) {
            // Detect format and create appropriate reader
            auto format = ReaderFactory::detectFormat(path);
            auto reader = ReaderFactory::createReader<Resource>(format);

            // vfspp adds / to the root by default
            std::filesystem::path vfsPath = "/" / path;
            vfspp::IFilePtr file = _vfs->OpenFile(PathUtils::createNormalizedFileInfo(vfsPath), vfspp::IFile::FileMode::Read);

            if (!file || !file->IsOpened()) {
                throw std::runtime_error{ "Failed to open file from VFS: " + vfsPath.string() };
            }
            std::vector<uint8_t> data(file->Size());
            file->Read(data, file->Size());
            _resources.emplace(path.string(), std::move(reader->openFile(path.string(), data)));
        }

        return dynamic_cast<Resource*>(_resources.at(path.string()).get());
    }

    bool exists(const std::string& filename);
    bool fileExistsInVFS(const std::filesystem::path& filepath) const;
    void insertTexture(const std::string& filename);

    const sf::Texture& texture(const std::string& filename);

    std::string FIDtoFrmName(unsigned int FID);

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
};

} // namespace geck
