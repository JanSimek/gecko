#pragma once

#include "SelectionManager.h"
#include "../format/map/Map.h"
#include "../editor/Object.h"
#include <SFML/Graphics.hpp>
#include <array>
#include <functional>

namespace geck::selection {

/**
 * @brief Bridge class that connects SelectionManager with existing EditorWidget functionality
 * 
 * This class acts as an adapter between SelectionManager and EditorWidget,
 * providing Qt signal integration for the selection system.
 */
class SelectionBridge {
public:
    // Function types for callbacks to EditorWidget methods
    using ObjectHitTestFunc = std::function<std::vector<std::shared_ptr<Object>>(sf::Vector2f)>;
    using TileHitTestFunc = std::function<std::optional<int>(sf::Vector2f, bool)>; // worldPos, isRoof
    using SpriteClickTestFunc = std::function<bool(sf::Vector2f, const sf::Sprite&)>;
    using TilePositionFunc = std::function<sf::Vector2f(int)>; // tileIndex -> worldPos
    using GetAllObjectsFunc = std::function<std::vector<std::shared_ptr<Object>>()>;
    
    // Sprite access functions
    using FloorSpritesFunc = std::function<std::array<sf::Sprite, Map::TILES_PER_ELEVATION>&()>;
    using RoofSpritesFunc = std::function<std::array<sf::Sprite, Map::TILES_PER_ELEVATION>&()>;
    
    // Map data access functions
    using GetCurrentElevationFunc = std::function<int()>;
    using GetMapFileFunc = std::function<Map::MapFile&()>;
    
    explicit SelectionBridge(SelectionManager& selectionManager);
    
    // Setup callbacks to EditorWidget methods
    void setObjectHitTest(ObjectHitTestFunc func) { _objectHitTest = std::move(func); }
    void setTileHitTest(TileHitTestFunc func) { _tileHitTest = std::move(func); }
    void setSpriteClickTest(SpriteClickTestFunc func) { _spriteClickTest = std::move(func); }
    void setTilePositionFunc(TilePositionFunc func) { _tilePosition = std::move(func); }
    void setFloorSpritesFunc(FloorSpritesFunc func) { _floorSprites = std::move(func); }
    void setRoofSpritesFunc(RoofSpritesFunc func) { _roofSprites = std::move(func); }
    void setGetAllObjectsFunc(GetAllObjectsFunc func) { _getAllObjects = std::move(func); }
    void setGetCurrentElevationFunc(GetCurrentElevationFunc func) { _getCurrentElevation = std::move(func); }
    void setGetMapFileFunc(GetMapFileFunc func) { _getMapFile = std::move(func); }
    
    // Bridge methods that implement the actual hit detection logic
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos, int elevation);
    std::optional<int> getRoofTileAtPosition(sf::Vector2f worldPos, int elevation);
    std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos, int elevation);
    std::optional<int> getFloorTileAtPosition(sf::Vector2f worldPos, int elevation);
    std::vector<int> getTilesInArea(const sf::FloatRect& area, bool roof, int elevation);
    std::vector<int> getTilesInAreaIncludingEmpty(const sf::FloatRect& area, bool roof, int elevation);
    std::vector<std::shared_ptr<Object>> getObjectsInArea(const sf::FloatRect& area, int elevation);
    std::vector<std::shared_ptr<Object>> getAllObjects();
    
    // Sprite collision detection helper
    bool isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite);
    
    // Cycling logic implementation
    SelectionResult cycleThroughItemsAtPosition(sf::Vector2f worldPos, int elevation);
    
private:
    SelectionManager& _selectionManager;
    
    // Callback functions
    ObjectHitTestFunc _objectHitTest;
    TileHitTestFunc _tileHitTest;
    SpriteClickTestFunc _spriteClickTest;
    TilePositionFunc _tilePosition;
    FloorSpritesFunc _floorSprites;
    RoofSpritesFunc _roofSprites;
    GetAllObjectsFunc _getAllObjects;
    GetCurrentElevationFunc _getCurrentElevation;
    GetMapFileFunc _getMapFile;
    
    // Internal helpers
    bool isPositionInTileSprite(sf::Vector2f worldPos, int tileIndex, bool roof);
    SelectionResult selectItem(SelectionType type, const std::variant<int, std::shared_ptr<Object>>& item);
};


} // namespace geck::selection