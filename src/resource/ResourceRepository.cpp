#include "ResourceRepository.h"

namespace geck::resource {

ResourceRepository::ResourceRepository(DataFileSystem& files)
    : _files(files) {
}

void ResourceRepository::clear() {
    _resources.clear();
}

std::string ResourceRepository::normalizeKey(const std::filesystem::path& path) {
    return path.generic_string();
}

} // namespace geck::resource
