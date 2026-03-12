#pragma once

#include <filesystem>
#include <memory>
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
};

} // namespace geck::resource
