#pragma once

#include <cstdint>

#include "DataFileSystem.h"
#include "FrmResolver.h"
#include "ResourceRepository.h"
#include "TextureManager.h"

namespace geck {
class Pro;
}

namespace geck::resource {

class GameResources final {
public:
    GameResources();

    [[nodiscard]] DataFileSystem& files();

    [[nodiscard]] ResourceRepository& repository();

    /// Loads the proto for an engine PID through the standard path resolution
    /// (ProHelper::basePath). Convenience for the ubiquitous
    /// `repository().load<Pro>(ProHelper::basePath(*this, pid))` idiom; returns the cached
    /// pointer and propagates load failures exactly as the underlying repository does.
    [[nodiscard]] Pro* loadPro(uint32_t pid);

    [[nodiscard]] FrmResolver& frmResolver();

    [[nodiscard]] TextureManager& textures();

    void clearCaches();
    void clearAllDataPaths();

private:
    DataFileSystem _files;
    ResourceRepository _repository;
    FrmResolver _frmResolver;
    TextureManager _textures;
};

} // namespace geck::resource
