#include "MapRenderer.h"
#include "../../util/ResourceManager.h"
#include "../../util/TileUtils.h"
#include "../../util/ColorUtils.h"
#include "../../util/StringUtils.h"
#include "../../util/Constants.h"
#include "../../format/lst/Lst.h"

#include <spdlog/spdlog.h>

namespace geck::ui::components {

MapRenderer::MapRenderer(Map* map) : _map(map) {
    if (!_map) {
        throw std::invalid_argument("Map cannot be null");
    }
}

void MapRenderer::renderTiles(sf::RenderWindow& window, int elevation, bool showRoof) {
    window.setView(_currentView);
    
    // Render floor tiles
    for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        const auto& sprite = _floorSprites[i];
        if (sprite.getTexture()) {
            window.draw(sprite);
        }
    }
    
    // Render roof tiles if enabled
    if (showRoof && _showRoof) {
        for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
            const auto& sprite = _roofSprites[i];
            if (sprite.getTexture()) {
                window.draw(sprite);
            }
        }
    }
}

void MapRenderer::renderObjects(sf::RenderWindow& window, const std::vector<std::shared_ptr<Object>>& objects, 
                               bool showObjects, bool showCritters, bool showWalls) {
    window.setView(_currentView);
    
    if (!showObjects && !_showObjects) {
        return;
    }
    
    for (const auto& object : objects) {
        if (!object) continue;
        
        // For now, render all objects if any object visibility flag is enabled
        // TODO: Implement proper object type detection based on pro_pid
        if (showObjects || showCritters || showWalls) {
            window.draw(object->getSprite());
        }
    }
}

void MapRenderer::renderSelectionIndicators(sf::RenderWindow& window, const std::vector<sf::RectangleShape>& indicators) {
    window.setView(_currentView);
    
    for (const auto& indicator : indicators) {
        window.draw(indicator);
    }
}

void MapRenderer::renderDragSelection(sf::RenderWindow& window, const sf::RectangleShape& selectionRect) {
    window.setView(_currentView);
    window.draw(selectionRect);
}

void MapRenderer::loadTileSprites(int elevation) {
    auto& resourceManager = ResourceManager::getInstance();
    const auto& mapFile = _map->getMapFile();
    
    // Load floor tiles
    for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        loadTileSprite(i, false, elevation); // Floor tiles
        loadTileSprite(i, true, elevation);  // Roof tiles
    }
    
    spdlog::info("MapRenderer: Loaded {} floor and roof tile sprites for elevation {}", Map::TILES_PER_ELEVATION, elevation);
}

void MapRenderer::updateTileSprite(int hexIndex, bool isRoof, int elevation) {
    if (hexIndex < 0 || hexIndex >= Map::TILES_PER_ELEVATION) {
        spdlog::warn("MapRenderer: Invalid tile index {} for sprite update", hexIndex);
        return;
    }
    
    loadTileSprite(hexIndex, isRoof, elevation);
}

void MapRenderer::reloadSprites(int elevation) {
    spdlog::info("MapRenderer: Reloading all sprites for elevation {}", elevation);
    loadTileSprites(elevation);
}

void MapRenderer::clearTileHighlights() {
    // Reset all tile sprites to normal color
    for (auto& sprite : _floorSprites) {
        sprite.setColor(sf::Color::White);
    }
    for (auto& sprite : _roofSprites) {
        sprite.setColor(sf::Color::White);
    }
}

void MapRenderer::highlightTile(int tileIndex, bool isRoof, sf::Color color) {
    if (tileIndex < 0 || tileIndex >= Map::TILES_PER_ELEVATION) {
        return;
    }
    
    if (isRoof) {
        _roofSprites[tileIndex].setColor(color);
    } else {
        _floorSprites[tileIndex].setColor(color);
    }
}

void MapRenderer::highlightTiles(const std::vector<int>& tileIndices, bool isRoof, sf::Color color) {
    for (int tileIndex : tileIndices) {
        highlightTile(tileIndex, isRoof, color);
    }
}

void MapRenderer::loadTileSprite(int tileIndex, bool isRoof, int elevation) {
    try {
        const auto& mapFile = _map->getMapFile();
        auto& resourceManager = ResourceManager::getInstance();
        
        // Get tile data for specified elevation
        int tileId;
        if (isRoof) {
            tileId = mapFile.tiles.at(elevation).at(tileIndex).getRoof();
        } else {
            tileId = mapFile.tiles.at(elevation).at(tileIndex).getFloor();
        }
        
        if (tileId == Map::EMPTY_TILE) {
            if (isRoof) {
                // Skip empty roof tiles - they should be handled by UI indicators
                sf::Sprite sprite(getBlankTexture());
                auto screenPos = geck::indexToScreenPosition(tileIndex, false);
                sprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - ROOF_OFFSET);
                _roofSprites[tileIndex] = sprite;
                return;
            } else {
                // Floor tiles use blank.frm for empty tiles
                sf::Sprite sprite(resourceManager.texture("art/tiles/blank.frm"));
                auto screenPos = geck::indexToScreenPosition(tileIndex, false);
                sprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
                _floorSprites[tileIndex] = sprite;
                return;
            }
        }
        
        // Load texture from resource manager using tiles.lst
        const auto& lst = resourceManager.getResource<Lst, std::string>("art/tiles/tiles.lst");
        const auto& texture = resourceManager.texture("art/tiles/" + lst->at(tileId));
        
        // Create sprite and position it
        sf::Sprite sprite(texture);
        
        // Validate texture was loaded
        if (!sprite.getTexture()) {
            if (isRoof) {
                spdlog::warn("MapRenderer: Failed to load texture for roof tile {} with ID {}", tileIndex, tileId);
                sf::Sprite blankSprite(getBlankTexture());
                auto screenPos = geck::indexToScreenPosition(tileIndex, false);
                blankSprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - ROOF_OFFSET);
                _roofSprites[tileIndex] = blankSprite;
            } else {
                spdlog::warn("MapRenderer: Failed to load texture for floor tile {} with ID {}", tileIndex, tileId);
                sf::Sprite blankSprite(getBlankTexture());
                auto screenPos = geck::indexToScreenPosition(tileIndex, false);
                blankSprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
                _floorSprites[tileIndex] = blankSprite;
            }
            return;
        }
        
        auto screenPos = geck::indexToScreenPosition(tileIndex, false); // Don't apply roof offset in utility function
        if (isRoof) {
            sprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - ROOF_OFFSET);
            _roofSprites[tileIndex] = sprite;
        } else {
            sprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
            _floorSprites[tileIndex] = sprite;
        }
        
    } catch (const std::exception& e) {
        spdlog::debug("MapRenderer: Failed to load sprite for tile {}: {}", tileIndex, e.what());
        
        // Create empty sprite for failed loads
        sf::Sprite blankSprite(getBlankTexture());
        auto screenPos = geck::indexToScreenPosition(tileIndex, false);
        if (isRoof) {
            blankSprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - ROOF_OFFSET);
            _roofSprites[tileIndex] = blankSprite;
        } else {
            blankSprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
            _floorSprites[tileIndex] = blankSprite;
        }
    }
}

sf::Vector2f MapRenderer::getTileWorldPosition(int tileIndex, bool isRoof) const {
    auto screenPos = geck::indexToScreenPosition(tileIndex, isRoof);
    return sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
}

const sf::Texture& MapRenderer::getBlankTexture() const {
    if (!_blankTexture) {
        _blankTexture = std::make_unique<sf::Texture>();
        // Create a 1x1 transparent texture for empty sprites
        sf::Image blankImage;
        blankImage.create(1, 1, sf::Color::Transparent);
        _blankTexture->loadFromImage(blankImage);
    }
    return *_blankTexture;
}

} // namespace geck::ui::components