#include "GameResources.h"

namespace geck::resource {

GameResources::GameResources()
    : _files()
    , _repository(_files)
    , _frmResolver(_repository)
    , _textures(_repository) {
}

DataFileSystem& GameResources::files() {
    return _files;
}

ResourceRepository& GameResources::repository() {
    return _repository;
}

FrmResolver& GameResources::frmResolver() {
    return _frmResolver;
}

TextureManager& GameResources::textures() {
    return _textures;
}

void GameResources::clearCaches() {
    _textures.clear();
    _repository.clear();
}

void GameResources::clearAllDataPaths() {
    clearCaches();
    _files.clear();
}

} // namespace geck::resource
