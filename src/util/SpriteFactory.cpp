#include "SpriteFactory.h"
#include "ResourceManager.h"
#include "Constants.h"
#include "TileUtils.h"
#include "../format/lst/Lst.h"
#include "../format/map/Map.h"

namespace geck {

sf::Sprite SpriteFactory::createTileSprite(
    const std::string& texturePath,
    const ScreenPosition& screenPos,
    int yOffset,
    const sf::Color& color) {
    
    sf::Sprite sprite(ResourceManager::getInstance().texture(texturePath));
    sprite.setPosition(sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - yOffset));
    sprite.setColor(color);
    return sprite;
}

sf::Sprite SpriteFactory::createEmptyTileSprite(
    const ScreenPosition& screenPos,
    int yOffset,
    const sf::Color& color) {
    
    return createTileSprite(BLANK_TILE_PATH, screenPos, yOffset, color);
}

sf::Sprite SpriteFactory::createFloorTileSprite(
    uint16_t tileId,
    const ScreenPosition& screenPos) {
    
    if (tileId == Map::EMPTY_TILE) {
        return createEmptyTileSprite(screenPos);
    }
    
    const auto& lst = ResourceManager::getInstance().getResource<Lst, std::string>("art/tiles/tiles.lst");
    const std::string texturePath = TILES_PATH_PREFIX + lst->at(tileId);
    return createTileSprite(texturePath, screenPos);
}

sf::Sprite SpriteFactory::createRoofTileSprite(
    uint16_t tileId,
    const ScreenPosition& screenPos) {
    
    if (tileId == Map::EMPTY_TILE) {
        return createEmptyTileSprite(screenPos, ROOF_OFFSET, TileColors::transparent());
    }
    
    const auto& lst = ResourceManager::getInstance().getResource<Lst, std::string>("art/tiles/tiles.lst");
    const std::string texturePath = TILES_PATH_PREFIX + lst->at(tileId);
    return createTileSprite(texturePath, screenPos, ROOF_OFFSET);
}

} // namespace geck