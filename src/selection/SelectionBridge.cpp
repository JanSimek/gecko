#include "SelectionBridge.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace geck::selection {

SelectionBridge::SelectionBridge(SelectionManager& selectionManager)
    : _selectionManager(selectionManager) {
}

std::vector<std::shared_ptr<Object>> SelectionBridge::getObjectsAtPosition(sf::Vector2f worldPos, int elevation) {
    if (_objectHitTest) {
        return _objectHitTest(worldPos);
    }
    return {};
}

std::optional<int> SelectionBridge::getRoofTileAtPosition(sf::Vector2f worldPos, int elevation) {
    if (_tileHitTest) {
        return _tileHitTest(worldPos, true); // true for roof
    }
    return std::nullopt;
}

std::optional<int> SelectionBridge::getFloorTileAtPosition(sf::Vector2f worldPos, int elevation) {
    if (_tileHitTest) {
        return _tileHitTest(worldPos, false); // false for floor
    }
    return std::nullopt;
}

std::vector<int> SelectionBridge::getTilesInArea(const sf::FloatRect& area, bool roof, int elevation) {
    std::vector<int> result;
    
    if (!_tilePosition) {
        return result;
    }
    
    // Get access to sprite arrays to check actual tile bounds
    auto* floorSprites = _floorSprites ? &_floorSprites() : nullptr;
    auto* roofSprites = _roofSprites ? &_roofSprites() : nullptr;
    
    if (!floorSprites || !roofSprites) {
        return result;
    }
    
    // Check each tile to see if its sprite bounds intersect with the area
    for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        sf::FloatRect tileBounds;
        
        if (roof) {
            tileBounds = roofSprites->at(i).getGlobalBounds();
        } else {
            tileBounds = floorSprites->at(i).getGlobalBounds();
        }
        
        // Check if tile bounds intersect with selection area
        if (area.intersects(tileBounds)) {
            result.push_back(i);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<Object>> SelectionBridge::getObjectsInArea(const sf::FloatRect& area, int elevation) {
    std::vector<std::shared_ptr<Object>> result;
    
    if (!_getAllObjects) {
        return result;
    }
    
    // Get all objects and check which ones are in the area
    auto allObjects = _getAllObjects();
    for (auto& object : allObjects) {
        // Get object sprite bounds
        const auto& sprite = object->getSprite();
        sf::FloatRect objectBounds = sprite.getGlobalBounds();
        
        // Check if object bounds intersect with selection area
        if (area.intersects(objectBounds)) {
            result.push_back(object);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<Object>> SelectionBridge::getAllObjects() {
    if (_getAllObjects) {
        return _getAllObjects();
    }
    return {};
}

bool SelectionBridge::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    if (_spriteClickTest) {
        return _spriteClickTest(worldPos, sprite);
    }
    return sprite.getGlobalBounds().contains(worldPos);
}

SelectionResult SelectionBridge::cycleThroughItemsAtPosition(sf::Vector2f worldPos, int elevation) {
    // Implementation of the existing cycling logic from EditorWidget
    
    // Get all available items at this position
    auto objectsAtPos = getObjectsAtPosition(worldPos, elevation);
    auto roofTileIndex = getRoofTileAtPosition(worldPos, elevation);
    auto floorTileIndex = getFloorTileAtPosition(worldPos, elevation);
    
    // Get current selection
    const auto& selection = _selectionManager.getCurrentSelection();
    
    // Check what's currently selected at this position
    bool roofSelected = false;
    bool floorSelected = false;
    int selectedObjectIndex = -1;
    
    // Check if there's an item at this position that's currently selected
    for (const auto& item : selection.items) {
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
            return selectItem(SelectionType::OBJECT, objectsAtPos[0]);
        } else if (floorTileIndex) {
            return selectItem(SelectionType::FLOOR_TILE, floorTileIndex.value());
        } else {
            // No objects or floor tiles available, clear selection
            _selectionManager.clearSelection();
            return SelectionResult::createSuccess();
        }
    } else if (selectedObjectIndex >= 0) {
        // An object is selected, cycle to next object or move to floor
        if (selectedObjectIndex < static_cast<int>(objectsAtPos.size()) - 1) {
            // Select next object
            return selectItem(SelectionType::OBJECT, objectsAtPos[selectedObjectIndex + 1]);
        } else if (floorTileIndex) {
            // No more objects, select floor
            return selectItem(SelectionType::FLOOR_TILE, floorTileIndex.value());
        } else {
            // No floor tile available, clear selection
            _selectionManager.clearSelection();
            return SelectionResult::createSuccess();
        }
    } else if (floorSelected) {
        // Floor is selected, deselect everything
        _selectionManager.clearSelection();
        return SelectionResult::createSuccess();
    } else {
        // Nothing selected, start with roof or first available
        if (roofTileIndex) {
            return selectItem(SelectionType::ROOF_TILE, roofTileIndex.value());
        } else if (!objectsAtPos.empty()) {
            return selectItem(SelectionType::OBJECT, objectsAtPos[0]);
        } else if (floorTileIndex) {
            return selectItem(SelectionType::FLOOR_TILE, floorTileIndex.value());
        }
    }
    
    // If we get here, there's nothing to select - clear selection
    _selectionManager.clearSelection();
    return SelectionResult::createSuccess();
}

bool SelectionBridge::isPositionInTileSprite(sf::Vector2f worldPos, int tileIndex, bool roof) {
    // This will need access to the sprite positioning logic
    // For now, use the tile position function and basic bounds checking
    if (!_tilePosition) {
        return false;
    }
    
    sf::Vector2f tilePos = _tilePosition(tileIndex);
    if (roof) {
        tilePos.y -= 96; // Tile::ROOF_OFFSET
    }
    
    // Basic rectangular bounds check (80x36 tile size)
    const float TILE_WIDTH = 80.0f;
    const float TILE_HEIGHT = 36.0f;
    
    return worldPos.x >= tilePos.x - TILE_WIDTH/2 &&
           worldPos.x <= tilePos.x + TILE_WIDTH/2 &&
           worldPos.y >= tilePos.y - TILE_HEIGHT/2 &&
           worldPos.y <= tilePos.y + TILE_HEIGHT/2;
}

SelectionResult SelectionBridge::selectItem(SelectionType type, const std::variant<int, std::shared_ptr<Object>>& item) {
    // Clear current selection
    _selectionManager.clearSelection();
    
    // Create the new selection item
    SelectedItem selectedItem;
    if (type == SelectionType::OBJECT) {
        selectedItem = SelectedItem{type, std::get<std::shared_ptr<Object>>(item)};
    } else {
        selectedItem = SelectedItem{type, std::get<int>(item)};
    }
    
    // Manually add to selection and set mode
    auto& selection = const_cast<Selection&>(_selectionManager.getCurrentSelection());
    selection.items.push_back(selectedItem);
    selection.mode = SelectionMode::ALL;
    
    // Trigger observer notifications using the now-public method
    _selectionManager.notifyObservers();
    
    return SelectionResult::createSuccess();
}

// QtSelectionObserver implementation
void QtSelectionObserver::onSelectionChanged(const Selection& selection) {
    // Always update visual appearance (this handles both selection and deselection)
    updateSelectionVisuals(selection);
    
    if (selection.isEmpty()) {
        onSelectionCleared();
        return;
    }
    
    // For now, emit signals for the first selected item to maintain compatibility
    // In the future, this could be enhanced to handle multiple selections
    const auto& firstItem = selection.items[0];
    
    switch (firstItem.type) {
        case SelectionType::OBJECT:
            if (_objectSelected) {
                _objectSelected(firstItem.getObject());
            }
            break;
            
        case SelectionType::ROOF_TILE:
            if (_tileSelected) {
                _tileSelected(firstItem.getTileIndex(), _currentElevation, true);
            }
            break;
            
        case SelectionType::FLOOR_TILE:
            if (_tileSelected) {
                _tileSelected(firstItem.getTileIndex(), _currentElevation, false);
            }
            break;
    }
    
    // Log multiple selection info
    if (selection.items.size() > 1) {
        spdlog::info("Multiple items selected: {} total", selection.items.size());
    }
}

void QtSelectionObserver::updateSelectionVisuals(const Selection& selection) {
    // Use the visual update callback to update sprite colors and object selection states
    if (_updateVisuals) {
        _updateVisuals(selection);
    }
}

void QtSelectionObserver::onSelectionCleared() {
    if (_selectionCleared) {
        _selectionCleared();
    }
}

} // namespace geck::selection