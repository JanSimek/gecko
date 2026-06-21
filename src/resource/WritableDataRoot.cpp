#include "resource/WritableDataRoot.h"

#include "resource/DataFileSystem.h"

#include <fstream>
#include <stdexcept>

namespace geck::resource {

std::filesystem::path ensureWritableCopy(const DataFileSystem& files,
    const std::filesystem::path& writableRoot, const std::string& vfsRelPath) {
    const std::filesystem::path dest = writableRoot / vfsRelPath;
    if (std::filesystem::exists(dest)) {
        return dest; // already a writable copy (possibly already edited) — do not clobber it
    }

    const auto bytes = files.readRawBytes(vfsRelPath);
    if (!bytes.has_value()) {
        throw std::runtime_error("ensureWritableCopy: cannot read '" + vfsRelPath + "' from the mounted data");
    }

    std::filesystem::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("ensureWritableCopy: cannot write " + dest.string());
    }
    out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    return dest;
}

} // namespace geck::resource
