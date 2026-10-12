#include "resource/VfsSourceProvider.h"

#include "resource/DataFileSystem.h"

namespace geck::resource {

std::optional<std::string> VfsSourceProvider::read(const std::filesystem::path& path) const {
    const auto bytes = _files.readRawBytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    return std::string(bytes->begin(), bytes->end());
}

} // namespace geck::resource
