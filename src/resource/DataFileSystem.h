#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <vfspp/VirtualFileSystem.hpp>

namespace geck::resource {

struct MountedSourceInfo {
    enum class Kind {
        Directory,
        Dat,
    };

    Kind kind;
    std::filesystem::path sourcePath;
    std::string displayLabel;
};

/**
 * @brief Thread-safe facade over the vfspp virtual file system.
 *
 * Background loaders read game data concurrently with the main thread, but
 * vfspp's VirtualFileSystem mutates internal state (opened-file tracking, the
 * mounted-filesystem list) on every OpenFile/AddFileSystem call without any
 * synchronization of its own. All access therefore goes through _mutex. The
 * mutex is recursive because addDataPath() mounts nested archives by calling
 * itself.
 */
class DataFileSystem final {
public:
    DataFileSystem();

    void clear();
    void addDataPath(const std::filesystem::path& path);

    [[nodiscard]] std::optional<std::vector<uint8_t>> readRawBytes(const std::filesystem::path& path) const;
    [[nodiscard]] bool exists(const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<std::filesystem::path> list(const std::string& pattern = "*") const;
    std::optional<MountedSourceInfo> sourceInfo(const std::filesystem::path& path) const;

private:
    static std::filesystem::path normalizeVfsPath(const std::filesystem::path& path);
    static std::string globToRegexPattern(const std::string& pattern);

    vfspp::VirtualFileSystemPtr _vfs;
    mutable std::recursive_mutex _mutex;
};

} // namespace geck::resource
