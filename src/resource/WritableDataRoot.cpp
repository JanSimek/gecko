#include "resource/WritableDataRoot.h"

#include "resource/DataFileSystem.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace geck::resource {

bool sameDataPathEntry(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec)) {
        return true;
    }
    return a.lexically_normal() == b.lexically_normal();
}

std::optional<std::filesystem::path> findWritableDataPath(const std::vector<std::filesystem::path>& dataPaths) {
    for (auto it = dataPaths.rbegin(); it != dataPaths.rend(); ++it) {
        std::error_code ec;
        if (std::filesystem::is_directory(*it, ec)) {
            return *it; // last directory wins, so its copies shadow archives mounted before it
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findWritableDataPath(const std::vector<std::filesystem::path>& dataPaths,
    const std::filesystem::path& preferred) {
    if (!preferred.empty()) {
        const bool listed = std::any_of(dataPaths.begin(), dataPaths.end(),
            [&preferred](const std::filesystem::path& entry) { return sameDataPathEntry(entry, preferred); });
        std::error_code ec;
        if (listed && std::filesystem::is_directory(preferred, ec)) {
            return preferred;
        }
        spdlog::warn("Configured save location '{}' is {} — falling back to the highest-priority folder",
            preferred.string(), listed ? "not an existing directory" : "no longer among the data paths");
    }
    return findWritableDataPath(dataPaths);
}

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
