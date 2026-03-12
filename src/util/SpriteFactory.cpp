#include "SpriteFactory.h"
#include "../resource/GameResources.h"
#include "Constants.h"
#include "TileUtils.h"
#include "../format/lst/Lst.h"
#include "../format/map/Map.h"

namespace geck {

sf::Sprite SpriteFactory::createTileSprite(
    resource::GameResources& resources,
    const std::string& texturePath,
    const ScreenPosition& screenPos,
    int yOffset,
    const sf::Color& color) {

    sf::Sprite sprite(resources.textures().get(texturePath));
    sprite.setPosition(sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - yOffset));
    sprite.setColor(color);
    return sprite;
}

sf::Sprite SpriteFactory::createEmptyTileSprite(
    resource::GameResources& resources,
    const ScreenPosition& screenPos,
    int yOffset,
    const sf::Color& color) {

    return createTileSprite(resources, BLANK_TILE_PATH, screenPos, yOffset, color);
}

sf::Sprite SpriteFactory::createFloorTileSprite(
    resource::GameResources& resources,
    uint16_t tileId,
    const ScreenPosition& screenPos) {

    if (tileId == Map::EMPTY_TILE) {
        return createEmptyTileSprite(resources, screenPos);
    }

    const auto* lst = resources.repository().load<Lst>("art/tiles/tiles.lst");
    const std::string texturePath = TILES_PATH_PREFIX + lst->at(tileId);
    return createTileSprite(resources, texturePath, screenPos);
}

sf::Sprite SpriteFactory::createRoofTileSprite(
    resource::GameResources& resources,
    uint16_t tileId,
    const ScreenPosition& screenPos) {

    if (tileId == Map::EMPTY_TILE) {
        return createEmptyTileSprite(resources, screenPos, ROOF_OFFSET, TileColors::transparent());
    }

    const auto* lst = resources.repository().load<Lst>("art/tiles/tiles.lst");
    const std::string texturePath = TILES_PATH_PREFIX + lst->at(tileId);
    return createTileSprite(resources, texturePath, screenPos, ROOF_OFFSET);
}

} // namespace geck
