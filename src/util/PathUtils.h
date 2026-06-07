#pragma once

#include <filesystem>
#include <string>

namespace geck {

/**
 * @brief Path utilities for cross-platform VFS handling.
 *
 * The VFS layer always addresses files with forward slashes so that lookup
 * paths match the keys stored in the file list regardless of the host platform.
 */
class PathUtils {
public:
    /**
     * @brief Forward-slash virtual path string for vfspp lookups.
     *
     * vfspp addresses files by virtual path string (e.g. "/art/tiles/tile.frm").
     * Converting through generic_string() normalises Windows back-slashes to the
     * forward slashes used in DAT archives and the VFS layer.
     */
    static std::string toVfsPath(const std::filesystem::path& path)
    {
        return path.generic_string();
    }

    /**
     * @brief Normalize path separators to forward slashes.
     */
    static std::filesystem::path normalize(const std::filesystem::path& path)
    {
        return std::filesystem::path(path.generic_string());
    }
};

} // namespace geck
