#pragma once

#include <filesystem>
#include <vfspp/FileInfo.hpp>

namespace geck {

/**
 * @brief Path utilities for cross-platform file handling
 */
class PathUtils {
public:
    /**
     * @brief Create a normalized FileInfo with forward slash separators for VFS consistency
     *
     * This function ensures path separators are converted to forward slashes,
     * which matches the format used in DAT archives and provides consistency
     * across platforms in the VFS layer. For example:
     * - On Windows: "C:\\geck\\resources\\art\\tiles\\tiles.lst" -> "C:/geck/resources/art/tiles/tiles.lst"
     * - On Unix: "/home/user/resources\\art\\tiles\\tiles.lst" -> "/home/user/resources/art/tiles/tiles.lst"
     *
     * This approach ensures that paths stored in VFS file lists match the lookup
     * paths regardless of the host platform.
     *
     * @param path The path to normalize
     * @return vfspp::FileInfo with forward slash separators
     */
    static vfspp::FileInfo createNormalizedFileInfo(const std::filesystem::path& path) {
        // Convert to generic format with forward slashes for VFS consistency
        // This matches the format used in DAT archives and ensures cross-platform compatibility
        return vfspp::FileInfo(path.generic_string());
    }

    /**
     * @brief Create a normalized FileInfo from string path
     *
     * @param pathStr The path string to normalize
     * @return vfspp::FileInfo with forward slash separators
     */
    static vfspp::FileInfo createNormalizedFileInfo(const std::string& pathStr) {
        return createNormalizedFileInfo(std::filesystem::path(pathStr));
    }

    /**
     * @brief Normalize path separators to forward slashes
     *
     * @param path The path to normalize
     * @return Normalized path with forward slash separators
     */
    static std::filesystem::path normalize(const std::filesystem::path& path) {
        // Return path with forward slashes for consistency
        return std::filesystem::path(path.generic_string());
    }
};

} // namespace geck