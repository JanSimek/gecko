#include "GameResources.h"

#include "format/pro/Pro.h"
#include "util/ProHelper.h"

#include <spdlog/spdlog.h>

#include <exception>
#include <string>

namespace geck::resource {

GameResources::GameResources()
    : _files()
    , _repository(_files)
    , _frmResolver(_repository)
    , _textures(_repository, _files) {
}

DataFileSystem& GameResources::files() {
    return _files;
}

ResourceRepository& GameResources::repository() {
    return _repository;
}

Pro* GameResources::loadPro(uint32_t pid) {
    return _repository.load<Pro>(ProHelper::basePath(*this, pid));
}

Pro* GameResources::tryLoadPro(uint32_t pid) {
    std::string path;
    try {
        path = ProHelper::basePath(*this, pid);
    } catch (const std::exception& e) { // NOSONAR: basePath reports a missing list or an out-of-range PID as runtime_error or IOException
        spdlog::debug("tryLoadPro: no proto for pid {:#x}: {}", pid, e.what());
        return nullptr;
    }
    return _repository.tryLoad<Pro>(path);
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
