#include "SelectionManager.h"
#include "SelectionBridge.h"
#include "../format/map/MapObject.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace geck::selection {

// Selection helper methods
std::vector<int> Selection::getRoofTileIndices() const {
    std::vector<int> indices;
    for (const auto& item : items) {
        if (item.type == SelectionType::ROOF_TILE) {
            indices.push_back(item.getTileIndex());
        }
    }
    return indices;
}

std::vector<int> Selection::getFloorTileIndices() const {
    std::vector<int> indices;
    for (const auto& item : items) {
        if (item.type == SelectionType::FLOOR_TILE) {
            indices.push_back(item.getTileIndex());
        }
    }
    return indices;
}

std::vector<std::shared_ptr<Object>> Selection::getObjects() const {
    std::vector<std::shared_ptr<Object>> objects;
    for (const auto& item : items) {
        if (item.type == SelectionType::OBJECT) {
            objects.push_back(item.getObject());
        }
    }
    return objects;
}

// SelectionManager implementation
SelectionManager::SelectionManager(Map* map) 
    : _map(map) {
    if (!_map) {
        throw std::invalid_argument("Map cannot be null");
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
        
        case SelectionMode::ALL:
            return SelectionResult::createError("Area selection not supported in ALL mode");
            
        default:
            return SelectionResult::createError("Invalid selection mode");
    }
    
    _currentSelection.mode = mode;
    notifyObservers();
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
                _currentSelection.mode = mode;
                notifyObservers();
                return SelectionResult::createSuccess("");
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _currentSelection.mode = mode;
                notifyObservers();
                return SelectionResult::createSuccess("");
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _currentSelection.mode = mode;
                notifyObservers();
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
                _currentSelection.mode = mode;
                notifyObservers();
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
                    _currentSelection.mode = mode;
                    notifyObservers();
                    return SelectionResult::createSuccess("");
                }
            }
            // Then try objects
            {
                auto objects = getObjectsAtPosition(worldPos, currentElevation);
                if (!objects.empty()) {
                    SelectedItem item{SelectionType::OBJECT, objects[0]};
                    addItemToSelection(item);
                    _currentSelection.mode = mode;
                    notifyObservers();
                    return SelectionResult::createSuccess("");
                }
            }
            // Finally try floor tile
            {
                auto tileIndex = getFloorTileAtPosition(worldPos, currentElevation);
                if (tileIndex) {
                    SelectedItem item{SelectionType::FLOOR_TILE, tileIndex.value()};
                    addItemToSelection(item);
                    _currentSelection.mode = mode;
                    notifyObservers();
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
            _currentSelection.mode = mode;
            notifyObservers();
            return SelectionResult::createSuccess("Item removed from selection");
        } else {
            // Item is not selected - add it
            addItemToSelection(itemAtPosition.value());
            _currentSelection.mode = mode;
            notifyObservers();
            return SelectionResult::createSuccess("Item added to selection");
        }
    }
    
    return SelectionResult::createNoChange();
}

bool SelectionManager::startDrag(sf::Vector2f worldPos) {
    if (_currentSelection.isEmpty()) {
        return false;
    }
    
    // Check if any selected item is at this position
    // For now, we'll implement basic drag start logic
    _currentSelection.isDragging = true;
    _currentSelection.dragStartPosition = worldPos;
    
    spdlog::debug("Started drag operation at ({:.2f}, {:.2f})", worldPos.x, worldPos.y);
    return true;
}

void SelectionManager::updateDrag(sf::Vector2f currentPos) {
    if (!_currentSelection.isDragging) {
        return;
    }
    
    // Update drag position - this will be used for visual feedback
    // The actual moving logic will be implemented later
    spdlog::debug("Updating drag to ({:.2f}, {:.2f})", currentPos.x, currentPos.y);
}

SelectionResult SelectionManager::finishDrag(sf::Vector2f endPos) {
    if (!_currentSelection.isDragging) {
        return SelectionResult::createError("No drag operation in progress");
    }
    
    _currentSelection.isDragging = false;
    
    // Calculate the offset
    sf::Vector2f offset = endPos - _currentSelection.dragStartPosition;
    
    spdlog::info("Drag completed: offset ({:.2f}, {:.2f})", offset.x, offset.y);
    
    // TODO: Implement actual item moving logic here
    // For now, just report success
    notifyObservers();
    return SelectionResult::createSuccess();
}

void SelectionManager::cancelDrag() {
    _currentSelection.isDragging = false;
    spdlog::debug("Drag operation cancelled");
}

bool SelectionManager::startAreaSelection(sf::Vector2f startPos, SelectionMode mode) {
    if (mode == SelectionMode::ALL) {
        return false; // Area selection not supported in ALL mode
    }
    
    _currentSelection.selectionArea = sf::FloatRect(startPos.x, startPos.y, 0, 0);
    _currentSelection.mode = mode;
    
    spdlog::debug("Started area selection at ({:.2f}, {:.2f})", startPos.x, startPos.y);
    return true;
}

void SelectionManager::updateAreaSelection(sf::Vector2f currentPos) {
    if (!_currentSelection.selectionArea) {
        return;
    }
    
    auto& rect = _currentSelection.selectionArea.value();
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
    if (!_currentSelection.selectionArea) {
        return SelectionResult::createError("No area selection in progress");
    }
    
    auto area = _currentSelection.selectionArea.value();
    auto mode = _currentSelection.mode;
    
    // Clear the area selection state but keep the mode
    _currentSelection.selectionArea.reset();
    
    // Perform the actual area selection
    // Note: We need to get current elevation from somewhere - this will need to be passed in
    // For now, we'll use elevation 0
    return selectArea(area, mode, 0);
}

void SelectionManager::cancelAreaSelection() {
    _currentSelection.selectionArea.reset();
    spdlog::debug("Area selection cancelled");
}

void SelectionManager::clearSelection() {
    bool hadSelection = !_currentSelection.isEmpty();
    _currentSelection.clear();
    
    if (hadSelection) {
        notifyObservers();
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
            if (_bridge) {
                auto allObjects = _bridge->getAllObjects();
                for (auto& object : allObjects) {
                    SelectedItem item{SelectionType::OBJECT, object};
                    addItemToSelection(item);
                }
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
    
    _currentSelection.mode = mode;
    notifyObservers();
}

void SelectionManager::addObserver(std::weak_ptr<SelectionObserver> observer) {
    _observers.push_back(observer);
}

void SelectionManager::removeObserver(std::weak_ptr<SelectionObserver> observer) {
    _observers.erase(
        std::remove_if(_observers.begin(), _observers.end(),
            [&observer](const std::weak_ptr<SelectionObserver>& weak) {
                return weak.expired() || weak.lock() == observer.lock();
            }),
        _observers.end()
    );
}

bool SelectionManager::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) const {
    return sprite.getGlobalBounds().contains(worldPos);
}

// Private helper methods
std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsAtPosition(sf::Vector2f worldPos, int elevation) const {
    if (_bridge) {
        return _bridge->getObjectsAtPosition(worldPos, elevation);
    }
    return {};
}

std::optional<int> SelectionManager::getRoofTileAtPosition(sf::Vector2f worldPos, int elevation) const {
    if (_bridge) {
        return _bridge->getRoofTileAtPosition(worldPos, elevation);
    }
    return std::nullopt;
}

std::optional<int> SelectionManager::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos, int elevation) const {
    if (_bridge) {
        return _bridge->getRoofTileAtPositionIncludingEmpty(worldPos, elevation);
    }
    return std::nullopt;
}

std::optional<int> SelectionManager::getFloorTileAtPosition(sf::Vector2f worldPos, int elevation) const {
    if (_bridge) {
        return _bridge->getFloorTileAtPosition(worldPos, elevation);
    }
    return std::nullopt;
}

std::vector<int> SelectionManager::getTilesInArea(const sf::FloatRect& area, bool roof, int elevation) const {
    if (_bridge) {
        return _bridge->getTilesInArea(area, roof, elevation);
    }
    return {};
}

std::vector<int> SelectionManager::getTilesInAreaIncludingEmpty(const sf::FloatRect& area, bool roof, int elevation) const {
    if (_bridge) {
        return _bridge->getTilesInAreaIncludingEmpty(area, roof, elevation);
    }
    return {};
}

std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsInArea(const sf::FloatRect& area, int elevation) const {
    if (_bridge) {
        return _bridge->getObjectsInArea(area, elevation);
    }
    return {};
}

SelectionResult SelectionManager::selectSingleAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation) {
    clearSelection();
    
    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::FLOOR_TILE, tileIndex.value()};
                addItemToSelection(item);
                _currentSelection.mode = mode;
                notifyObservers();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _currentSelection.mode = mode;
                notifyObservers();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{SelectionType::ROOF_TILE, tileIndex.value()};
                addItemToSelection(item);
                _currentSelection.mode = mode;
                notifyObservers();
                return SelectionResult::createSuccess();
            }
            break;
        }
        
        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos, elevation);
            if (!objects.empty()) {
                SelectedItem item{SelectionType::OBJECT, objects[0]}; // Select first (topmost)
                addItemToSelection(item);
                _currentSelection.mode = mode;
                notifyObservers();
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
    if (_bridge) {
        return _bridge->cycleThroughItemsAtPosition(worldPos, elevation);
    }
    return SelectionResult::createNoChange();
}

void SelectionManager::notifyObservers() {
    // Clean up expired observers
    _observers.erase(
        std::remove_if(_observers.begin(), _observers.end(),
            [](const std::weak_ptr<SelectionObserver>& weak) {
                return weak.expired();
            }),
        _observers.end()
    );
    
    // Notify remaining observers
    for (auto& weakObserver : _observers) {
        if (auto observer = weakObserver.lock()) {
            if (_currentSelection.isEmpty()) {
                observer->onSelectionCleared();
            } else {
                observer->onSelectionChanged(_currentSelection);
            }
        }
    }
}

void SelectionManager::addItemToSelection(const SelectedItem& item) {
    if (!isItemSelected(item)) {
        _currentSelection.items.push_back(item);
    }
}

void SelectionManager::removeItemFromSelection(const SelectedItem& item) {
    _currentSelection.items.erase(
        std::remove_if(_currentSelection.items.begin(), _currentSelection.items.end(),
            [&item](const SelectedItem& selected) {
                if (selected.type != item.type) {
                    return false;
                }
                
                if (item.isTile()) {
                    return selected.getTileIndex() == item.getTileIndex();
                } else {
                    return selected.getObject() == item.getObject();
                }
            }),
        _currentSelection.items.end()
    );
}

bool SelectionManager::isItemSelected(const SelectedItem& item) const {
    return std::any_of(_currentSelection.items.begin(), _currentSelection.items.end(),
        [&item](const SelectedItem& selected) {
            if (selected.type != item.type) {
                return false;
            }
            
            if (item.isTile()) {
                return selected.getTileIndex() == item.getTileIndex();
            } else {
                return selected.getObject() == item.getObject();
            }
        });
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