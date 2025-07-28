#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include "TileUtils.h"

namespace geck {

/**
 * @brief Factory class for creating commonly used sprites with consistent configuration
 */
class SpriteFactory {
public:
    /**
     * @brief Create a tile sprite at the specified screen position
     * @param texturePath Path to the texture resource
     * @param screenPos Screen position for the sprite
     * @param yOffset Optional Y-axis offset (e.g., for roof tiles)
     * @param color Optional color tint (default: white)
     * @return Configured sf::Sprite
     */
    [[nodiscard]] static sf::Sprite createTileSprite(
        const std::string& texturePath,
        const ScreenPosition& screenPos,
        int yOffset = 0,
        const sf::Color& color = sf::Color::White);

    /**
     * @brief Create an empty/blank tile sprite
     * @param screenPos Screen position for the sprite
     * @param yOffset Optional Y-axis offset
     * @param color Optional color tint (default: transparent for empty tiles)
     * @return Configured sf::Sprite for empty tile
     */
    [[nodiscard]] static sf::Sprite createEmptyTileSprite(
        const ScreenPosition& screenPos,
        int yOffset = 0,
        const sf::Color& color = sf::Color::Transparent);

    /**
     * @brief Create a floor tile sprite from tile ID
     * @param tileId Tile ID from the tiles.lst
     * @param screenPos Screen position
     * @return Configured floor tile sprite
     */
    [[nodiscard]] static sf::Sprite createFloorTileSprite(
        uint16_t tileId,
        const ScreenPosition& screenPos);

    /**
     * @brief Create a roof tile sprite from tile ID
     * @param tileId Tile ID from the tiles.lst
     * @param screenPos Screen position
     * @return Configured roof tile sprite with proper offset
     */
    [[nodiscard]] static sf::Sprite createRoofTileSprite(
        uint16_t tileId,
        const ScreenPosition& screenPos);

private:
    static constexpr const char* BLANK_TILE_PATH = "art/tiles/blank.frm";
    static constexpr const char* TILES_PATH_PREFIX = "art/tiles/";
};

} // namespace geck