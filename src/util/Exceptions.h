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
        : std::runtime_error("[Gecko] " + message) {}

    explicit GeckoException(const std::string& message, const std::string& context)
        : std::runtime_error("[Gecko] " + message + " (context: " + context + ")") {}
};

/**
 * @brief Exception for invalid arguments or parameter validation failures
 */
class InvalidArgumentException : public GeckoException {
public:
    explicit InvalidArgumentException(const std::string& message)
        : GeckoException("Invalid argument: " + message) {}

    explicit InvalidArgumentException(const std::string& message, const std::string& parameterName)
        : GeckoException("Invalid argument for '" + parameterName + "': " + message) {}
};

/**
 * @brief Exception for resource loading and management errors  
 */
class ResourceException : public GeckoException {
public:
    explicit ResourceException(const std::string& message)
        : GeckoException("Resource error: " + message) {}

    explicit ResourceException(const std::string& message, const std::filesystem::path& resourcePath)
        : GeckoException("Resource error: " + message + " (resource: " + resourcePath.string() + ")") {}
};

/**
 * @brief Exception for FRM file and sprite creation errors
 */
class SpriteException : public ResourceException {
public:
    explicit SpriteException(const std::string& message)
        : ResourceException("Sprite error: " + message) {}

    explicit SpriteException(const std::string& message, const std::filesystem::path& frmPath)
        : ResourceException("Sprite error: " + message, frmPath) {}
};

/**
 * @brief Exception for object creation and manipulation errors
 */
class ObjectException : public GeckoException {
public:
    explicit ObjectException(const std::string& message)
        : GeckoException("Object error: " + message) {}

    explicit ObjectException(const std::string& message, uint32_t objectId)
        : GeckoException("Object error: " + message + " (object ID: " + std::to_string(objectId) + ")") {}
};

/**
 * @brief Exception for map operations and coordinate system errors
 */
class MapException : public GeckoException {
public:
    explicit MapException(const std::string& message)
        : GeckoException("Map error: " + message) {}

    explicit MapException(const std::string& message, int elevation)
        : GeckoException("Map error: " + message + " (elevation: " + std::to_string(elevation) + ")") {}

    explicit MapException(const std::string& message, int hexPosition, int elevation)
        : GeckoException("Map error: " + message + " (hex: " + std::to_string(hexPosition) + 
                        ", elevation: " + std::to_string(elevation) + ")") {}
};

/**
 * @brief Exception for selection system errors
 */
class SelectionException : public GeckoException {
public:
    explicit SelectionException(const std::string& message)
        : GeckoException("Selection error: " + message) {}
};

/**
 * @brief Exception for UI and widget-related errors
 */
class UIException : public GeckoException {
public:
    explicit UIException(const std::string& message)
        : GeckoException("UI error: " + message) {}

    explicit UIException(const std::string& message, const std::string& componentName)
        : GeckoException("UI error in " + componentName + ": " + message) {}
};

/**
 * @brief Exception for configuration and settings errors
 */
class ConfigurationException : public GeckoException {
public:
    explicit ConfigurationException(const std::string& message)
        : GeckoException("Configuration error: " + message) {}

    explicit ConfigurationException(const std::string& message, const std::string& settingName)
        : GeckoException("Configuration error for '" + settingName + "': " + message) {}
};


} // namespace geck