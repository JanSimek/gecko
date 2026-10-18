#pragma once

#include <stdexcept>
#include <string>
#include <filesystem>

namespace geck {

/**
 * @brief Base exception class for all Gecko application errors
 *
 * Provides a unified exception hierarchy with consistent error formatting
 * and context information.
 */
class GeckoException : public std::runtime_error {
public:
    explicit GeckoException(const std::string& message)
        : std::runtime_error("[Gecko] " + message) { }

    explicit GeckoException(const std::string& message, const std::string& context)
        : std::runtime_error("[Gecko] " + message + " (context: " + context + ")") { }
};

/**
 * @brief Exception for resource loading and management errors
 */
class ResourceException : public GeckoException {
public:
    explicit ResourceException(const std::string& message)
        : GeckoException("Resource error: " + message) { }

    explicit ResourceException(const std::string& message, const std::filesystem::path& resourcePath)
        : GeckoException("Resource error: " + message + " (resource: " + resourcePath.string() + ")") { }
};

/**
 * @brief Exception for FRM file and sprite creation errors
 */
class SpriteException : public ResourceException {
public:
    explicit SpriteException(const std::string& message)
        : ResourceException("Sprite error: " + message) { }

    explicit SpriteException(const std::string& message, const std::filesystem::path& frmPath)
        : ResourceException("Sprite error: " + message, frmPath) { }
};

/**
 * @brief Exception for object creation and manipulation errors
 */
class ObjectException : public GeckoException {
public:
    explicit ObjectException(const std::string& message)
        : GeckoException("Object error: " + message) { }

    explicit ObjectException(const std::string& message, uint32_t objectId)
        : GeckoException("Object error: " + message + " (object ID: " + std::to_string(objectId) + ")") { }
};

} // namespace geck
