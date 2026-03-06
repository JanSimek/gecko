#pragma once

#include <vector>
#include <string>
#include <optional>
#include <filesystem>

namespace geck::resource {

/**
 * Interface for loading raw resource data from various sources
 * (filesystem, archives, network, etc.)
 */
class IResourceProvider {
public:
    virtual ~IResourceProvider() = default;

    /**
     * Load raw bytes from the specified path
     * @param path Resource path (can be virtual)
     * @return Raw data or empty optional on failure
     */
    virtual std::optional<std::vector<uint8_t>> load(const std::filesystem::path& path) = 0;

    /**
     * Check if a resource exists at the given path
     * @param path Resource path to check
     * @return true if resource exists
     */
    virtual bool exists(const std::filesystem::path& path) const = 0;

    /**
     * List all available resources matching a pattern
     * @param pattern Glob pattern (e.g., "*.frm")
     * @return List of matching resource paths
     */
    virtual std::vector<std::filesystem::path> list(const std::string& pattern = "*") const = 0;
};

} // namespace geck::resource