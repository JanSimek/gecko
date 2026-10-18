#include "SpatialIndex.h"

#include "format/map/Map.h"

namespace geck {

TileSpatialIndex::TileSpatialIndex()
    : _floorIndex(TILE_WIDTH)
    , _roofIndex(TILE_WIDTH) {
}

void TileSpatialIndex::buildIndex(const std::vector<sf::Sprite>& floorSprites,
    const std::vector<sf::Sprite>& roofSprites) {
    _floorIndex.clear();
    _roofIndex.clear();
    _indexedTiles = 0;

    // Every sprite carries a texture (real or blank) under SFML 3, so no null check is needed
    for (size_t i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        const auto& floorSprite = floorSprites[i];
        sf::FloatRect bounds = floorSprite.getGlobalBounds();
        _floorIndex.addItem(static_cast<int>(i), bounds);
        _indexedTiles++;
    }

    for (size_t i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        const auto& roofSprite = roofSprites[i];
        sf::FloatRect bounds = roofSprite.getGlobalBounds();
        _roofIndex.addItem(static_cast<int>(i), bounds);
        _indexedTiles++;
    }
}

std::vector<int> TileSpatialIndex::getTilesInArea(sf::FloatRect area, bool roof) const {
    const auto& index = roof ? _roofIndex : _floorIndex;
    return index.queryArea(area);
}

} // namespace geck