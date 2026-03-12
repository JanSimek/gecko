#pragma once

#include "DataFileSystem.h"
#include "FrmResolver.h"
#include "ResourceRepository.h"
#include "TextureManager.h"

namespace geck::resource {

class GameResources final {
public:
    GameResources();

    [[nodiscard]] DataFileSystem& files();

    [[nodiscard]] ResourceRepository& repository();

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
