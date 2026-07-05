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
 * synchronization of its own. All access therefore goes through _mutex. Mounting
 * a directory also mounts its nested archives; that recursion runs through the
 * private addDataPathLocked() helper so the public lock is taken exactly once.
 */
class DataFileSystem final {
public:
    DataFileSystem();

    void clear();
    void addDataPath(const std::filesystem::path& path);

    /// Re-scan directory (native) mounts so files created/overwritten on disk this session become
    /// visible. vfspp's NativeFileSystem builds its file list once at mount and never rescans, so a
    /// file written through a plain stream (e.g. a saved .gam under a writable data path) is otherwise
    /// invisible to readRawBytes until the next launch. DAT mounts are immutable and left untouched.
    void refresh();

    [[nodiscard]] std::optional<std::vector<uint8_t>> readRawBytes(const std::filesystem::path& path) const;
    [[nodiscard]] bool exists(const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<std::filesystem::path> list(const std::string& pattern = "*") const;
    std::optional<MountedSourceInfo> sourceInfo(const std::filesystem::path& path) const;

    /// Every initialized mount, in the order the data paths were added. Lookups probe the
    /// mounts in the REVERSE of this order (see sourceInfo), so a later entry shadows an
    /// earlier one that provides the same path.
    [[nodiscard]] std::vector<MountedSourceInfo> mounts() const;

private:
    // Mounts a path with _mutex already held; addDataPath() is the public locking entry point.
    void addDataPathLocked(const std::filesystem::path& path);

    static std::filesystem::path normalizeVfsPath(const std::filesystem::path& path);
    static std::string globToRegexPattern(const std::string& pattern);

    vfspp::VirtualFileSystemPtr _vfs;
    mutable std::mutex _mutex;
};

} // namespace geck::resource
