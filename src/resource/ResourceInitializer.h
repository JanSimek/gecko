#pragma once

#include <stdexcept>

namespace geck {

namespace resource {
    class GameResources;
}

/**
 * @brief Utility class for initializing essential game resources
 *
 * This class centralizes one-time resource bootstrap and validation logic.
 */
class ResourceInitializer {
public:
    /**
     * @brief Load all essential LST files required for object palette functionality
     *
     * This loads all the LST files needed by FrmResolver for FID to FRM name resolution.
     * Must be called before using object palettes or creating objects.
     *
     * @throws std::exception if any essential LST file fails to load
     */
    static void loadEssentialLstFiles(resource::GameResources& resources);

    /**
     * @brief Verify that essential LST files have already been loaded into the repository
     *
     * This does not touch the data files. It only confirms that startup bootstrap completed.
     *
     * @throws std::exception if any essential LST file is missing from the repository cache
     */
    static void requireEssentialLstFilesLoaded(resource::GameResources& resources);

    /**
     * @brief Load essential textures for basic map functionality
     *
     * This loads textures that are commonly needed for map display and object visualization.
     *
     * @throws std::exception if any essential texture fails to load
     */
    static void loadEssentialTextures(resource::GameResources& resources);

private:
    // Static utility class - no instantiation
    ResourceInitializer() = delete;
    ~ResourceInitializer() = delete;
    ResourceInitializer(const ResourceInitializer&) = delete;
    ResourceInitializer& operator=(const ResourceInitializer&) = delete;
};

} // namespace geck
