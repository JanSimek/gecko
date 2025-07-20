#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <array>
#include <vector>

#include "../../format/map/Map.h"
#include "../../editor/Object.h"
#include "../../util/Constants.h"

namespace geck::ui::components {

/**
 * @brief Responsible for rendering map tiles and objects
 * 
 * This component handles all rendering operations for the map editor,
 * including tile sprites, object sprites, and visual indicators.
 */
class MapRenderer {
public:
    explicit MapRenderer(Map* map);
    ~MapRenderer() = default;

    // Core rendering operations
    void renderTiles(sf::RenderWindow& window, int elevation, bool showRoof = true);
    void renderObjects(sf::RenderWindow& window, const std::vector<std::shared_ptr<Object>>& objects, bool showObjects = true, bool showCritters = true, bool showWalls = true);
    void renderSelectionIndicators(sf::RenderWindow& window, const std::vector<sf::RectangleShape>& indicators);
    void renderDragSelection(sf::RenderWindow& window, const sf::RectangleShape& selectionRect);
    
    // Sprite management
    void loadTileSprites(int elevation = 0);
    void updateTileSprite(int hexIndex, bool isRoof, int elevation = 0);
    void reloadSprites(int elevation = 0);
    
    // Access to sprite arrays (for collision detection)
    const std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& getFloorSprites() const { return _floorSprites; }
    const std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& getRoofSprites() const { return _roofSprites; }
    
    // Visual effect methods
    void clearTileHighlights();
    void highlightTile(int tileIndex, bool isRoof, sf::Color color);
    void highlightTiles(const std::vector<int>& tileIndices, bool isRoof, sf::Color color);
    
    // Visibility controls
    void setShowObjects(bool show) { _showObjects = show; }
    void setShowCritters(bool show) { _showCritters = show; }
    void setShowWalls(bool show) { _showWalls = show; }
    void setShowRoof(bool show) { _showRoof = show; }
    void setShowScrollBlk(bool show) { _showScrollBlk = show; }
    
    // View management
    void setView(const sf::View& view) { _currentView = view; }
    const sf::View& getView() const { return _currentView; }

private:
    Map* _map;
    sf::View _currentView;
    
    // Sprite arrays for rendering
    std::array<sf::Sprite, Map::TILES_PER_ELEVATION> _floorSprites;
    std::array<sf::Sprite, Map::TILES_PER_ELEVATION> _roofSprites;
    
    // Blank texture for empty sprites (SFML 3 compatibility)
    mutable std::unique_ptr<sf::Texture> _blankTexture;
    
    // Visibility flags
    bool _showObjects = true;
    bool _showCritters = true;
    bool _showRoof = true;
    bool _showWalls = true;
    bool _showScrollBlk = true;
    
    // Helper methods
    void loadTileSprite(int tileIndex, bool isRoof, int elevation = 0);
    sf::Vector2f getTileWorldPosition(int tileIndex, bool isRoof) const;
    const sf::Texture& getBlankTexture() const;
};

} // namespace geck::ui::components