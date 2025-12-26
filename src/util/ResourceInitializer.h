#pragma once

#include <stdexcept>

namespace geck {

/**
 * @brief Utility class for initializing essential game resources
 *
 * This class consolidates resource loading patterns to avoid duplication
 * and ensure consistent resource initialization across the application.
 */
class ResourceInitializer {
public:
    /**
     * @brief Load all essential LST files required for object palette functionality
     *
     * This loads all the LST files needed by the ResourceManager for FID to FRM name resolution.
     * Must be called before using object palettes or creating objects.
     *
     * @throws std::exception if any essential LST file fails to load
     */
    static void loadEssentialLstFiles();

    /**
     * @brief Load essential textures for basic map functionality
     *
     * This loads textures that are commonly needed for map display and object visualization.
     *
     * @throws std::exception if any essential texture fails to load
     */
    static void loadEssentialTextures();

    /**
     * @brief Load all essential resources for new map creation
     *
     * Convenience method that calls both loadEssentialLstFiles() and loadEssentialTextures().
     * Use this when creating a new map from scratch.
     *
     * @throws std::exception if any essential resource fails to load
     */
    static void loadEssentialResources();

private:
    // Static utility class - no instantiation
    ResourceInitializer() = delete;
    ~ResourceInitializer() = delete;
    ResourceInitializer(const ResourceInitializer&) = delete;
    ResourceInitializer& operator=(const ResourceInitializer&) = delete;
};

} // namespace geck