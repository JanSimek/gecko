#include "SpatialIndex.h"
#include "TileUtils.h"
#include <cmath>
#include <algorithm>

namespace geck {

TileSpatialIndex::TileSpatialIndex() : _floorIndex(TILE_WIDTH), _roofIndex(TILE_WIDTH) {
    // Initialize with tile-optimized cell size
}

void TileSpatialIndex::buildIndex(const std::array<sf::Sprite, TILES_PER_ELEVATION>& floorSprites,
                                 const std::array<sf::Sprite, TILES_PER_ELEVATION>& roofSprites) {
    // Clear existing indices
    _floorIndex.clear();
    _roofIndex.clear();
    _indexedTiles = 0;
    
    // Build floor tile index
    for (int i = 0; i < TILES_PER_ELEVATION; ++i) {
        const auto& floorSprite = floorSprites[i];
        // All sprites now have textures (either real texture or blank texture for SFML 3 compatibility)
        sf::FloatRect bounds = floorSprite.getGlobalBounds();
        _floorIndex.addItem(i, bounds);
        _indexedTiles++;
    }
    
    // Build roof tile index  
    for (int i = 0; i < TILES_PER_ELEVATION; ++i) {
        const auto& roofSprite = roofSprites[i];
        // All sprites now have textures (either real texture or blank texture for SFML 3 compatibility)  
        sf::FloatRect bounds = roofSprite.getGlobalBounds();
        _roofIndex.addItem(i, bounds);
        _indexedTiles++;
    }
}

std::vector<int> TileSpatialIndex::getTilesInArea(sf::FloatRect area, bool roof) const {
    const auto& index = roof ? _roofIndex : _floorIndex;
    return index.queryArea(area);
}

std::vector<int> TileSpatialIndex::getTilesInRadius(sf::Vector2f center, float radius, bool roof) const {
    // Convert radius to bounding box for initial query
    sf::FloatRect area({center.x - radius, center.y - radius}, { radius * 2, radius * 2 });

    std::vector<int> candidates = getTilesInArea(area, roof);
    std::vector<int> results;
    results.reserve(candidates.size());
    
    // Filter candidates by actual distance
    float radiusSquared = radius * radius;
    for (int tileIndex : candidates) {
        auto screenPos = indexToScreenPosition(tileIndex, roof);
        sf::Vector2f tilePos(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
        
        float distanceSquared = (tilePos.x - center.x) * (tilePos.x - center.x) + 
                               (tilePos.y - center.y) * (tilePos.y - center.y);
        
        if (distanceSquared <= radiusSquared) {
            results.push_back(tileIndex);
        }
    }
    
    return results;
}

std::vector<int> TileSpatialIndex::getTilesAlongLine(sf::Vector2f start, sf::Vector2f end, bool roof) const {
    std::vector<int> results;
    
    // Use Bresenham-like algorithm adapted for hex grid
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    if (distance < 1.0f) {
        // Too short, just return tiles at start point
        return getTilesInRadius(start, TILE_WIDTH / 2, roof);
    }
    
    // Step along the line
    int steps = static_cast<int>(distance / (TILE_WIDTH / 2)); // Sample every half-tile
    float stepX = dx / steps;
    float stepY = dy / steps;
    
    std::unordered_set<int> uniqueTiles; // Avoid duplicates
    
    for (int i = 0; i <= steps; ++i) {
        sf::Vector2f currentPos(start.x + i * stepX, start.y + i * stepY);
        auto tilesAtPos = getTilesInRadius(currentPos, TILE_WIDTH / 4, roof);
        
        for (int tileIndex : tilesAtPos) {
            uniqueTiles.insert(tileIndex);
        }
    }
    
    results.reserve(uniqueTiles.size());
    for (int tileIndex : uniqueTiles) {
        results.push_back(tileIndex);
    }
    
    return results;
}

std::vector<int> TileSpatialIndex::getTilesInRectangle(sf::FloatRect rect, bool roof) const {
    return getTilesInArea(rect, roof);
}

std::vector<int> TileSpatialIndex::getTilesInCircle(sf::Vector2f center, float radius, bool roof) const {
    return getTilesInRadius(center, radius, roof);
}

sf::FloatRect TileSpatialIndex::getTileBounds(int tileIndex, bool roof) const {
    if (!isValidTileIndex(tileIndex)) {
        return sf::FloatRect();
    }
    
    auto screenPos = indexToScreenPosition(tileIndex, roof);
    sf::Vector2f position(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
    
    // Create bounds based on tile size
    return sf::FloatRect({ position.x - TILE_WIDTH / 2, position.y - TILE_HEIGHT / 2 },
        {TILE_WIDTH, TILE_HEIGHT});
}

bool TileSpatialIndex::isValidTileIndex(int index) const {
    return index >= 0 && index < TILES_PER_ELEVATION;
}

} // namespace geck