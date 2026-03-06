#pragma once

#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <algorithm>
#include "IResourceProvider.h"

namespace geck::resource {

/**
 * Interface for parsing raw data into specific resource types
 * @tparam T The resource type this loader produces
 */
template <typename T>
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;

    /**
     * Parse raw data into a resource object
     * @param data Raw bytes to parse
     * @param path Original path (for error reporting)
     * @return Parsed resource or nullptr on failure
     */
    virtual std::unique_ptr<T> parse(
        const std::vector<uint8_t>& data,
        const std::filesystem::path& path)
        = 0;

    /**
     * Get the file extensions this loader supports
     * @return List of extensions (e.g., {".frm", ".fr0"})
     */
    virtual std::vector<std::string> supportedExtensions() const = 0;

    /**
     * Check if this loader can handle the given path
     * @param path Path to check
     * @return true if this loader supports the file type
     */
    virtual bool canLoad(const std::filesystem::path& path) const {
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        const auto& exts = supportedExtensions();
        return std::find(exts.begin(), exts.end(), ext) != exts.end();
    }
};

} // namespace geck::resource