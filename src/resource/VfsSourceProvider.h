#pragma once

#include "ssl/Source.h"

namespace geck::resource {

class DataFileSystem;

/// Script sources read through the mounted data (loose files and DATs), for a script whose .ssl is
/// not under a marked script-source tree. Paths are VFS paths, e.g. "scripts/acvillgr.ssl".
class VfsSourceProvider final : public ssl::SourceProvider {
public:
    explicit VfsSourceProvider(const DataFileSystem& files)
        : _files(files) {}

    std::optional<std::string> read(const std::filesystem::path& path) const override;

private:
    const DataFileSystem& _files;
};

} // namespace geck::resource
