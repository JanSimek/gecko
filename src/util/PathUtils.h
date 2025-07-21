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
     * @brief Create a normalized FileInfo with native path separators
     *
     * This function addresses VFSPP compatibility by ensuring path separators
     * are converted to the native format. For example:
     * - On Windows: "C:\\geck\\resources/art/tiles/tiles.lst" -> "C:\\geck\\resources\\art\\tiles\\tiles.lst"
     * - On Unix: "/home/user/resources\\art\\tiles\\tiles.lst" -> "/home/user/resources/art/tiles/tiles.lst"
     *
     * This replaces the need for modifications in VFSPP's OpenFileST method.
     *
     * @param path The path to normalize
     * @return vfspp::FileInfo with normalized native path separators
     */
    static vfspp::FileInfo createNormalizedFileInfo(const std::filesystem::path& path) {
        // Convert to native format with make_preferred()
        std::filesystem::path nativePath = path;
        nativePath.make_preferred();
        return vfspp::FileInfo(nativePath.string());
    }

    /**
     * @brief Create a normalized FileInfo from string path
     *
     * @param pathStr The path string to normalize
     * @return vfspp::FileInfo with normalized native path separators
     */
    static vfspp::FileInfo createNormalizedFileInfo(const std::string& pathStr) {
        return createNormalizedFileInfo(std::filesystem::path(pathStr));
    }

    /**
     * @brief Normalize path separators to native format
     *
     * @param path The path to normalize
     * @return Normalized path with native separators
     */
    static std::filesystem::path normalize(const std::filesystem::path& path) {
        std::filesystem::path normalized = path;
        return normalized.make_preferred();
    }
};

} // namespace geck