#include "SelectionManager.h"
#include "../ui/EditorWidget.h"
#include "../format/map/MapObject.h"
#include "../util/Constants.h"
#include "../util/TileUtils.h"
#include <algorithm>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace geck::selection {

// SelectionManager implementation
SelectionManager::SelectionManager(Map* map, geck::EditorWidget* editorWidget) 
    : _map(map), _editorWidget(editorWidget) {
    if (!_map) {
        throw std::invalid_argument("Map cannot be null");
    }
    if (!_editorWidget) {
        throw std::invalid_argument("EditorWidget cannot be null");
    }
}

SelectionResult SelectionManager::selectAtPosition(sf::Vector2f worldPos, SelectionMode mode, int currentElevation) {
    switch (mode) {
        case SelectionMode::ALL:
            return cycleThroughItemsAtPosition(worldPos, currentElevation);
            
        case SelectionMode::OBJECTS:
        case SelectionMode::FLOOR_TILES:
        case SelectionMode::ROOF_TILES:
        case SelectionMode::ROOF_TILES_ALL:
            return selectSingleAtPosition(worldPos, mode, currentElevation);
            
        default:
            return SelectionResult::createError("Invalid selection mode");
    }
}

SelectionResult SelectionManager::selectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) {
    clearSelection();
    
    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tiles = getTilesInArea(area, false, currentElevation);
            for (int tileIndex : tiles) {
                SelectedItem item{SelectionType::FLOOR_TILE, tileIndex};
                addItemToSelection(item);
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES: {
            auto tiles = getTilesInArea(area, true, currentElevation);
            for (int tileIndex : tiles) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex};
                addItemToSelection(item);
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES_ALL: {
            auto tiles = getTilesInAreaIncludingEmpty(area, true, currentElevation);
            for (int tileIndex : tiles) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex};
                addItemToSelection(item);
            }
            break;
        }
        
        case SelectionMode::OBJECTS: {
            auto objects = getObjectsInArea(area, currentElevation);
            for (auto& object : objects) {
                SelectedItem item{SelectionType::OBJECT, object};
                addItemToSelection(item);
            }
            break;
        }
        
        case SelectionMode::ALL: {
            // Select all items in the area: objects, floor tiles, and roof tiles
            auto objects = getObjectsInArea(area, currentElevation);
            for (auto& object : objects) {
                SelectedItem item{SelectionType::OBJECT, object};
                addItemToSelection(item);
            }
            
            auto floorTiles = getTilesInArea(area, false, currentElevation);
            for (int tileIndex : floorTiles) {
                SelectedItem item{SelectionType::FLOOR_TILE, tileIndex};
                addItemToSelection(item);
            }
            
            auto roofTiles = getTilesInArea(area, true, currentElevation);
            for (int tileIndex : roofTiles) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex};
                addItemToSelection(item);
            }
            break;
        }
            
        default:
            return SelectionResult::createError("Invalid selection mode");
    }
    
    _state.mode = mode;
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

SelectionResult SelectionManager::addToSelection(sf::Vector2f worldPos, SelectionMode mode, int currentElevation) {
    // Don't clear existing selection - add to it
    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::FLOOR_TILE, tileIndex.value()};
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }
        
        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos, currentElevation);
            if (!objects.empty()) {
                // Add the first object found
                SelectedItem item{SelectionType::OBJECT, objects[0]};
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        case SelectionMode::ALL:
            // For ALL mode, find and add the first available item without cycling
            // Try roof tile first
            {
                auto tileIndex = getRoofTileAtPosition(worldPos, currentElevation);
                if (tileIndex) {
                    SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                    addItemToSelection(item);
                    _state.mode = mode;
                    notifySelectionChanged();
                    return SelectionResult::createSuccess("");
                }
            }
            // Then try objects
            {
                auto objects = getObjectsAtPosition(worldPos, currentElevation);
                if (!objects.empty()) {
                    SelectedItem item{SelectionType::OBJECT, objects[0]};
                    addItemToSelection(item);
                    _state.mode = mode;
                    notifySelectionChanged();
                    return SelectionResult::createSuccess("");
                }
            }
            // Finally try floor tile
            {
                auto tileIndex = getFloorTileAtPosition(worldPos, currentElevation);
                if (tileIndex) {
                    SelectedItem item{SelectionType::FLOOR_TILE, tileIndex.value()};
                    addItemToSelection(item);
                    _state.mode = mode;
                    notifySelectionChanged();
                    return SelectionResult::createSuccess();
                }
            }
            break;
            
        default:
            return SelectionResult::createError("Invalid selection mode");
    }
    
    return SelectionResult::createNoChange();
}

SelectionResult SelectionManager::toggleSelection(sf::Vector2f worldPos, SelectionMode mode, int currentElevation) {
    // Find what item is at this position without clearing current selection
    std::optional<SelectedItem> itemAtPosition;
    
    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos, currentElevation);
            if (tileIndex) {
                itemAtPosition = SelectedItem{SelectionType::FLOOR_TILE, tileIndex.value()};
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos, currentElevation);
            if (tileIndex) {
                itemAtPosition = SelectedItem{SelectionType::ROOF_TILE, tileIndex.value()};
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos, currentElevation);
            if (tileIndex) {
                itemAtPosition = SelectedItem{SelectionType::ROOF_TILE, tileIndex.value()};
            }
            break;
        }
        
        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos, currentElevation);
            if (!objects.empty()) {
                itemAtPosition = SelectedItem{SelectionType::OBJECT, objects[0]};
            }
            break;
        }
        
        case SelectionMode::ALL: {
            // For ALL mode, check in priority order: roof -> objects -> floor
            auto roofTileIndex = getRoofTileAtPosition(worldPos, currentElevation);
            if (roofTileIndex) {
                itemAtPosition = SelectedItem{SelectionType::ROOF_TILE, roofTileIndex.value()};
            } else {
                auto objects = getObjectsAtPosition(worldPos, currentElevation);
                if (!objects.empty()) {
                    itemAtPosition = SelectedItem{SelectionType::OBJECT, objects[0]};
                } else {
                    auto floorTileIndex = getFloorTileAtPosition(worldPos, currentElevation);
                    if (floorTileIndex) {
                        itemAtPosition = SelectedItem{SelectionType::FLOOR_TILE, floorTileIndex.value()};
                    }
                }
            }
            break;
        }
        
        default:
            return SelectionResult::createError("Invalid selection mode");
    }
    
    if (itemAtPosition) {
        if (isItemSelected(itemAtPosition.value())) {
            // Item is selected - remove it
            removeItemFromSelection(itemAtPosition.value());
            _state.mode = mode;
            notifySelectionChanged();
            return SelectionResult::createSuccess("Item removed from selection");
        } else {
            // Item is not selected - add it
            addItemToSelection(itemAtPosition.value());
            _state.mode = mode;
            notifySelectionChanged();
            return SelectionResult::createSuccess("Item added to selection");
        }
    }
    
    return SelectionResult::createNoChange();
}

bool SelectionManager::startDrag(sf::Vector2f worldPos) {
    if (_state.isEmpty()) {
        return false;
    }
    
    // Check if any selected item is at this position
    // For now, we'll implement basic drag start logic
    _state.isDragging = true;
    _state.dragStartPosition = worldPos;
    
    spdlog::debug("Started drag operation at ({:.2f}, {:.2f})", worldPos.x, worldPos.y);
    return true;
}

void SelectionManager::updateDrag(sf::Vector2f currentPos) {
    if (!_state.isDragging) {
        return;
    }
    
    // Update drag position - this will be used for visual feedback
    // The actual moving logic will be implemented later
    spdlog::debug("Updating drag to ({:.2f}, {:.2f})", currentPos.x, currentPos.y);
}

SelectionResult SelectionManager::finishDrag(sf::Vector2f endPos) {
    if (!_state.isDragging) {
        return SelectionResult::createError("No drag operation in progress");
    }
    
    _state.isDragging = false;
    
    // Calculate the offset
    sf::Vector2f offset = endPos - _state.dragStartPosition;
    
    spdlog::info("Drag completed: offset ({:.2f}, {:.2f})", offset.x, offset.y);
    
    // TODO: Implement actual item moving logic here
    // For now, just report success
    notifySelectionChanged();
    return SelectionResult::createSuccess();
}

void SelectionManager::cancelDrag() {
    _state.isDragging = false;
    spdlog::debug("Drag operation cancelled");
}

bool SelectionManager::startAreaSelection(sf::Vector2f startPos, SelectionMode mode) {
    _state.selectionArea = sf::FloatRect(startPos.x, startPos.y, 0, 0);
    _state.mode = mode;
    
    spdlog::debug("Started area selection at ({:.2f}, {:.2f}) for mode: {}", 
                 startPos.x, startPos.y, static_cast<int>(mode));
    return true;
}

void SelectionManager::updateAreaSelection(sf::Vector2f currentPos) {
    if (!_state.selectionArea) {
        return;
    }
    
    auto& rect = _state.selectionArea.value();
    sf::Vector2f startPos(rect.left, rect.top);
    
    // Update rectangle to encompass start and current position
    float left = std::min(startPos.x, currentPos.x);
    float top = std::min(startPos.y, currentPos.y);
    float right = std::max(startPos.x, currentPos.x);
    float bottom = std::max(startPos.y, currentPos.y);
    
    rect = sf::FloatRect(left, top, right - left, bottom - top);
    
    spdlog::debug("Updated area selection: ({:.2f}, {:.2f}, {:.2f}, {:.2f})", 
                 rect.left, rect.top, rect.width, rect.height);
}

SelectionResult SelectionManager::finishAreaSelection() {
    if (!_state.selectionArea) {
        return SelectionResult::createError("No area selection in progress");
    }
    
    auto area = _state.selectionArea.value();
    auto mode = _state.mode;
    
    // Clear the area selection state but keep the mode
    _state.selectionArea.reset();
    
    // Perform the actual area selection
    // Note: We need to get current elevation from somewhere - this will need to be passed in
    // For now, we'll use elevation 0
    return selectArea(area, mode, 0);
}

void SelectionManager::cancelAreaSelection() {
    _state.selectionArea.reset();
    spdlog::debug("Area selection cancelled");
}

void SelectionManager::clearSelection() {
    bool hadSelection = !_state.isEmpty();
    _state.clear();
    
    if (hadSelection) {
        notifySelectionChanged();
    }
}

void SelectionManager::selectAll(SelectionMode mode, int currentElevation) {
    clearSelection();
    
    switch (mode) {
        case SelectionMode::FLOOR_TILES:
            for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
                SelectedItem item{SelectionType::FLOOR_TILE, i};
                addItemToSelection(item);
            }
            break;
            
        case SelectionMode::ROOF_TILES:
            for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
                auto tile = _map->getMapFile().tiles.at(currentElevation).at(i);
                if (tile.getRoof() != Map::EMPTY_TILE) {
                    SelectedItem item{SelectionType::ROOF_TILE, i};
                    addItemToSelection(item);
                }
            }
            break;
            
        case SelectionMode::ROOF_TILES_ALL:
            for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
                // Include all roof tile positions, regardless of whether they have textures
                SelectedItem item{SelectionType::ROOF_TILE, i};
                addItemToSelection(item);
            }
            break;
            
        case SelectionMode::OBJECTS:
            // Get all objects from EditorWidget
            for (auto& object : _editorWidget->getObjects()) {
                SelectedItem item{SelectionType::OBJECT, object};
                addItemToSelection(item);
            }
            break;
            
        case SelectionMode::ALL:
            // Select all tiles and objects
            selectAll(SelectionMode::FLOOR_TILES, currentElevation);
            selectAll(SelectionMode::ROOF_TILES, currentElevation);
            selectAll(SelectionMode::OBJECTS, currentElevation);
            break;
            
        case SelectionMode::NUM_SELECTION_TYPES:
            // This should not happen - it's just an enum count
            break;
    }
    
    _state.mode = mode;
    notifySelectionChanged();
}


bool SelectionManager::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) const {
    return sprite.getGlobalBounds().contains(worldPos);
}

// Private helper methods
std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsAtPosition(sf::Vector2f worldPos, int elevation) const {
    return _editorWidget->getObjectsAtPosition(worldPos);
}

std::optional<int> SelectionManager::getRoofTileAtPosition(sf::Vector2f worldPos, int elevation) const {
    return _editorWidget->getTileAtPosition(worldPos, true); // true for roof
}

std::optional<int> SelectionManager::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos, int elevation) const {
    return _editorWidget->getRoofTileAtPositionIncludingEmpty(worldPos);
}

std::optional<int> SelectionManager::getFloorTileAtPosition(sf::Vector2f worldPos, int elevation) const {
    return _editorWidget->getTileAtPosition(worldPos, false); // false for floor
}

std::vector<int> SelectionManager::getTilesInArea(const sf::FloatRect& area, bool roof, int elevation) const {
    std::vector<int> result;
    result.reserve(1000); // Reserve space for typical area selection
    
    // Get access to sprite arrays to check actual tile bounds
    const auto& floorSprites = _editorWidget->getFloorSprites();
    const auto& roofSprites = _editorWidget->getRoofSprites();
    
    // Get current map data for checking tile content
    const auto& mapFile = _editorWidget->getMapFile();
    int currentElevation = _editorWidget->getCurrentElevation();
    
    // Check each tile to see if its sprite bounds intersect with the area
    for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        sf::FloatRect tileBounds;
        
        if (roof) {
            tileBounds = roofSprites.at(i).getGlobalBounds();
        } else {
            tileBounds = floorSprites.at(i).getGlobalBounds();
        }
        
        // Check if tile bounds intersect with selection area
        if (area.intersects(tileBounds)) {
            // For roof tiles, only include tiles that have actual content (not empty)
            if (roof) {
                if (mapFile.tiles.at(currentElevation).at(i).getRoof() != Map::EMPTY_TILE) {
                    result.push_back(i);
                }
            } else {
                // For floor tiles, include all tiles (floor tiles are always considered to have content)
                result.push_back(i);
            }
        }
    }
    
    return result;
}

std::vector<int> SelectionManager::getTilesInAreaIncludingEmpty(const sf::FloatRect& area, bool roof, int elevation) const {
    std::vector<int> result;
    result.reserve(1000); // Reserve space for typical area selection
    
    // Get access to sprite arrays to check actual tile bounds
    const auto& floorSprites = _editorWidget->getFloorSprites();
    const auto& roofSprites = _editorWidget->getRoofSprites();
    
    // Check each tile to see if its sprite bounds intersect with the area
    // This version includes empty tiles by not checking tile content
    for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        sf::FloatRect tileBounds;
        
        if (roof) {
            tileBounds = roofSprites.at(i).getGlobalBounds();
        } else {
            tileBounds = floorSprites.at(i).getGlobalBounds();
        }
        
        // Check if tile bounds intersect with selection area
        if (area.intersects(tileBounds)) {
            result.push_back(i);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsInArea(const sf::FloatRect& area, int elevation) const {
    std::vector<std::shared_ptr<Object>> result;
    result.reserve(100); // Reserve space for typical object selection
    
    // Get all objects from EditorWidget and check bounds intersection
    const auto& allObjects = _editorWidget->getObjects();
    
    for (const auto& object : allObjects) {
        // Check if object sprite bounds intersect with selection area
        const auto& sprite = object->getSprite();
        sf::FloatRect objectBounds = sprite.getGlobalBounds();
        
        // Use simple bounds intersection test (much faster than hit detection)
        if (area.intersects(objectBounds)) {
            result.push_back(object);
        }
    }
    
    return result;
}

SelectionResult SelectionManager::selectSingleAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation) {
    clearSelection();
    
    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::FLOOR_TILE, tileIndex.value()};
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos, elevation);
            if (!objects.empty()) {
                SelectedItem item{SelectionType::OBJECT, objects[0]}; // Select first (topmost)
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        default:
            return SelectionResult::createError("Invalid selection mode for single selection");
    }
    
    return SelectionResult::createNoChange();
}

SelectionResult SelectionManager::cycleThroughItemsAtPosition(sf::Vector2f worldPos, int elevation) {
    // Implementation of cycling logic without bridge
    
    // Get all available items at this position
    auto objectsAtPos = getObjectsAtPosition(worldPos, elevation);
    auto roofTileIndex = getRoofTileAtPosition(worldPos, elevation);
    auto floorTileIndex = getFloorTileAtPosition(worldPos, elevation);
    
    // Check what's currently selected at this position
    bool roofSelected = false;
    bool floorSelected = false;
    int selectedObjectIndex = -1;
    
    // Check if there's an item at this position that's currently selected
    for (const auto& item : _state.items) {
        switch (item.type) {
            case SelectionType::ROOF_TILE:
                if (roofTileIndex && item.getTileIndex() == roofTileIndex.value()) {
                    roofSelected = true;
                }
                break;
                
            case SelectionType::FLOOR_TILE:
                if (floorTileIndex && item.getTileIndex() == floorTileIndex.value()) {
                    floorSelected = true;
                }
                break;
                
            case SelectionType::OBJECT: {
                auto it = std::find(objectsAtPos.begin(), objectsAtPos.end(), item.getObject());
                if (it != objectsAtPos.end()) {
                    selectedObjectIndex = static_cast<int>(std::distance(objectsAtPos.begin(), it));
                }
                break;
            }
        }
    }
    
    // Selection logic: roof → objects (cycle through all) → floor → deselect
    if (roofSelected) {
        // Roof is selected, move to first object or floor if no objects
        if (!objectsAtPos.empty()) {
            clearSelection();
            SelectedItem item{SelectionType::OBJECT, objectsAtPos[0]};
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (floorTileIndex) {
            clearSelection();
            SelectedItem item{SelectionType::FLOOR_TILE, floorTileIndex.value()};
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else {
            clearSelection();
            return SelectionResult::createSuccess();
        }
    } else if (selectedObjectIndex >= 0) {
        // An object is selected, cycle to next object or move to floor
        if (selectedObjectIndex < static_cast<int>(objectsAtPos.size()) - 1) {
            // Select next object
            clearSelection();
            SelectedItem item{SelectionType::OBJECT, objectsAtPos[selectedObjectIndex + 1]};
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (floorTileIndex) {
            // No more objects, select floor
            clearSelection();
            SelectedItem item{SelectionType::FLOOR_TILE, floorTileIndex.value()};
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else {
            clearSelection();
            return SelectionResult::createSuccess();
        }
    } else if (floorSelected) {
        // Floor is selected, deselect everything
        clearSelection();
        return SelectionResult::createSuccess();
    } else {
        // Nothing selected, start with roof or first available
        if (roofTileIndex) {
            clearSelection();
            SelectedItem item{SelectionType::ROOF_TILE, roofTileIndex.value()};
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (!objectsAtPos.empty()) {
            clearSelection();
            SelectedItem item{SelectionType::OBJECT, objectsAtPos[0]};
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (floorTileIndex) {
            clearSelection();
            SelectedItem item{SelectionType::FLOOR_TILE, floorTileIndex.value()};
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        }
    }
    
    clearSelection();
    return SelectionResult::createSuccess();
}

void SelectionManager::notifySelectionChanged() {
    if (_selectionCallback) {
        _selectionCallback(_state);
    }
}

void SelectionManager::addItemToSelection(const SelectedItem& item) {
    _state.addItem(item);
}

void SelectionManager::removeItemFromSelection(const SelectedItem& item) {
    _state.removeItem(item);
}

bool SelectionManager::isItemSelected(const SelectedItem& item) const {
    return _state.hasItem(item);
}

sf::Vector2f SelectionManager::getTileWorldPosition(int tileIndex) const {
    // This will need to implement the tile positioning logic from EditorWidget
    // For now, return a placeholder
    return sf::Vector2f(0, 0);
}

bool SelectionManager::isPositionInTile(sf::Vector2f worldPos, int tileIndex, bool roof) const {
    // This will need to implement the tile hit detection logic from EditorWidget
    // For now, return false
    return false;
}

} // namespace geck::selection