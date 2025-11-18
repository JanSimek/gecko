#include "ExitGridPlacementManager.h"
#include "../core/EditorWidget.h"
#include "../core/MainWindow.h"
#include "../widgets/SFMLWidget.h"
#include "../viewport/ViewportController.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapObject.h"
#include "../../util/Coordinates.h"
#include "../../util/Constants.h"
#include "../../util/TileUtils.h"
#include "../../editor/HexagonGrid.h"
#include "../../selection/SelectionState.h"

#include <QApplication>
#include <QMessageBox>
#include <spdlog/spdlog.h>

namespace geck {

ExitGridPlacementManager::ExitGridPlacementManager(EditorWidget* editor)
    : _editor(editor) {
}

void ExitGridPlacementManager::setExitGridPlacementMode(bool enabled) {
    _exitGridPlacementMode = enabled;
    
    if (enabled) {
        spdlog::debug("Exit grid placement mode enabled");
    } else {
        spdlog::debug("Exit grid placement mode disabled");
    }
}

void ExitGridPlacementManager::setMarkExitsMode(bool enabled) {
    _markExitsMode = enabled;
    
    if (enabled) {
        spdlog::debug("Mark exits mode enabled");
    } else {
        spdlog::debug("Mark exits mode disabled");
    }
}

void ExitGridPlacementManager::handleExitGridPlacement(sf::Vector2f worldPos) {
    if (!_exitGridPlacementMode) {
        return;
    }
    
    placeExitGridAtPosition(worldPos);
}

void ExitGridPlacementManager::placeExitGridAtPosition(sf::Vector2f worldPos) {
    if (!_editor || !_editor->getMap()) {
        spdlog::error("Cannot place exit grid: no editor or map available");
        return;
    }

    // Convert world position to hex position
    auto hexPositionOpt = _editor->getViewportController()->worldPosToHexPosition(WorldCoords(worldPos.x, worldPos.y));
    if (!hexPositionOpt.has_value() || !hexPositionOpt->isValid()) {
        spdlog::error("Invalid hex position for exit grid placement");
        return;
    }
    int hexPosition = hexPositionOpt->value();

    // Show properties dialog for new exit grid
    ExitGridPropertiesDialog::ExitGridProperties properties;
    if (!showPropertiesDialog(properties)) {
        // User cancelled
        return;
    }

    // Create the exit grid object
    auto exitGridObject = createExitGridObject(hexPosition, properties);
    if (!exitGridObject) {
        QMessageBox::critical(_editor, "Error", "Failed to create exit grid object.");
        return;
    }

    // Add to map
    auto& mapFile = _editor->getMap()->getMapFile();
    int currentElevation = _editor->getCurrentElevation();
    
    // Add to the appropriate elevation level in the map
    mapFile.map_objects[currentElevation].push_back(exitGridObject);

    // Refresh objects in editor to show the new exit grid
    _editor->refreshObjects();
    
    spdlog::info("Placed exit grid at hex position {} (elevation {})", hexPosition, currentElevation);
}

void ExitGridPlacementManager::editExitGridProperties(std::shared_ptr<MapObject> exitGrid) {
    if (!exitGrid || !exitGrid->isExitGridMarker()) {
        return;
    }

    // Extract current properties
    ExitGridPropertiesDialog::ExitGridProperties currentProperties;
    currentProperties.exitMap = exitGrid->exit_map;
    currentProperties.exitPosition = exitGrid->exit_position;
    currentProperties.exitElevation = exitGrid->exit_elevation;
    currentProperties.exitOrientation = exitGrid->exit_orientation;

    // Show properties dialog
    ExitGridPropertiesDialog::ExitGridProperties newProperties;
    if (!showPropertiesDialog(newProperties, &currentProperties)) {
        // User cancelled
        return;
    }

    // Update the exit grid object
    exitGrid->exit_map = newProperties.exitMap;
    exitGrid->exit_position = newProperties.exitPosition;
    exitGrid->exit_elevation = newProperties.exitElevation;
    exitGrid->exit_orientation = newProperties.exitOrientation;

    // Refresh objects in editor to show updated properties
    _editor->refreshObjects();
    
    spdlog::info("Updated exit grid properties: map={}, pos={}, elev={}, orient={}", 
                 newProperties.exitMap, newProperties.exitPosition, 
                 newProperties.exitElevation, newProperties.exitOrientation);
}

std::shared_ptr<MapObject> ExitGridPlacementManager::createExitGridObject(int hexPosition, const ExitGridPropertiesDialog::ExitGridProperties& properties) {
    auto exitGrid = std::make_shared<MapObject>();
    
    // Set basic object properties
    exitGrid->unknown0 = 0;
    exitGrid->position = hexPosition;
    exitGrid->x = 0;  // These are calculated from position
    exitGrid->y = 0;
    exitGrid->sx = 0;
    exitGrid->sy = 0;
    exitGrid->frame_number = 0;
    exitGrid->direction = 0;
    
    // Exit grids don't have a visual FRM in the game - they're invisible
    // The FRM PID can be 0 or a placeholder
    exitGrid->frm_pid = 0x01000000; // Use a default misc FRM
    exitGrid->flags = 0;
    exitGrid->elevation = _editor->getCurrentElevation();
    
    // Set exit grid PID (MISC type with index 16-23)
    exitGrid->pro_pid = getAvailableExitGridPID();
    
    exitGrid->critter_index = -1;
    exitGrid->light_radius = 0;
    exitGrid->light_intensity = 0;
    exitGrid->outline_color = 0;
    exitGrid->map_scripts_pid = -1;
    exitGrid->script_id = -1;
    
    // Inventory
    exitGrid->objects_in_inventory = 0;
    exitGrid->max_inventory_size = 0;
    exitGrid->amount = 1;
    
    exitGrid->unknown10 = 0;
    exitGrid->unknown11 = 0;
    
    // Set exit grid properties
    exitGrid->exit_map = properties.exitMap;
    exitGrid->exit_position = properties.exitPosition;
    exitGrid->exit_elevation = properties.exitElevation;
    exitGrid->exit_orientation = properties.exitOrientation;
    
    // Clear other extra fields
    exitGrid->player_reaction = 0;
    exitGrid->current_mp = 0;
    exitGrid->combat_results = 0;
    exitGrid->dmg_last_turn = 0;
    exitGrid->ai_packet = 0;
    exitGrid->group_id = 0;
    exitGrid->who_hit_me = 0;
    exitGrid->current_hp = 0;
    exitGrid->current_rad = 0;
    exitGrid->current_poison = 0;
    exitGrid->ammo = 0;
    exitGrid->keycode = 0;
    exitGrid->ammo_pid = 0;
    exitGrid->elevhex = 0;
    exitGrid->map = 0;
    exitGrid->walkthrough = 0;
    exitGrid->elevtype = 0;
    exitGrid->elevlevel = 0;
    
    return exitGrid;
}

bool ExitGridPlacementManager::showPropertiesDialog(ExitGridPropertiesDialog::ExitGridProperties& properties, const ExitGridPropertiesDialog::ExitGridProperties* existing) {
    ExitGridPropertiesDialog dialog(existing ? *existing : ExitGridPropertiesDialog::ExitGridProperties{}, _editor);
    
    if (!existing) {
        // Initialize defaults for new exit grids
        ExitGridPropertiesDialog::ExitGridProperties defaults;
        defaults.exitMap = 0;
        defaults.exitPosition = 0;
        defaults.exitElevation = 0;
        defaults.exitOrientation = 0;
        dialog.setProperties(defaults);
    }
    
    // Simple dialog execution without additional modal settings
    int result = dialog.exec();
    if (result == QDialog::Accepted) {
        properties = dialog.getProperties();
        return true;
    }
    
    return false;
}

uint32_t ExitGridPlacementManager::getAvailableExitGridPID() const {
    // Exit grids use MISC type (0x05) with indices 16-23
    // For now, just use the first one (16)
    // TODO: Could check existing objects to find first available slot
    return 0x05000010; // MISC type | index 16
}

void ExitGridPlacementManager::resetState() {
    _exitGridPlacementMode = false;
    _markExitsMode = false;
}

bool ExitGridPlacementManager::editSelectedExitGrids() {
    if (!_editor) {
        return false;
    }
    
    auto* selectionManager = _editor->getSelectionManager();
    if (!selectionManager) {
        return false;
    }
    
    const auto& selectionState = selectionManager->getCurrentSelection();
    if (selectionState.isEmpty()) {
        return false;
    }
    
    // Find the first selected exit grid
    for (const auto& item : selectionState.items) {
        if (item.isObject()) {
            auto selectedObject = item.getObject();
            if (!selectedObject) {
                continue;
            }
            
            auto& mapObject = selectedObject->getMapObject();
            
            // Check if this is an exit grid
            if (mapObject.isExitGridMarker()) {
                // Get the actual MapObject shared_ptr from the selected object
                auto mapObjectPtr = selectedObject->getMapObjectPtr();
                if (mapObjectPtr) {
                    editExitGridProperties(mapObjectPtr);
                    return true;
                }
            }
        }
    }
    
    return false;
}

void ExitGridPlacementManager::handleMarkExitsSelection(sf::Vector2f worldPos) {
    if (!_markExitsMode || !_editor) {
        return;
    }
    
    // Find exit grids at the clicked position
    auto objects = _editor->getObjectsAtPosition(worldPos);
    std::vector<std::shared_ptr<Object>> exitGrids;
    
    for (auto& object : objects) {
        if (object && object->getMapObjectPtr() && object->getMapObjectPtr()->isExitGridMarker()) {
            exitGrids.push_back(object);
        }
    }
    
    if (!exitGrids.empty()) {
        
        // Clear current selection for visual feedback  
        auto* selectionManager = _editor->getSelectionManager();
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        
        // Also clear any drag selection preview
        _editor->clearSelection();
        
        // Highlight the selected exit grids BEFORE opening the dialog
        for (auto& exitGrid : exitGrids) {
            exitGrid->getSprite().setColor(geck::TileColors::exitGridHighlight());
        }
        
        // Force an immediate SFML render to show the highlight before dialog opens
        if (auto* sfmlWidget = _editor->getSFMLWidget()) {
            sfmlWidget->updateAndRender();
        }
        QApplication::processEvents();
        
        // Open properties dialog directly with the first exit grid found
        auto firstExitGrid = exitGrids[0];
        auto mapObjectPtr = firstExitGrid->getMapObjectPtr();
        if (mapObjectPtr) {
            editExitGridProperties(mapObjectPtr);
            
            // After dialog closes, clear the highlighting
            // Note: refreshObjects() might recreate objects, so we need to find them again
            auto objectsAfterRefresh = _editor->getObjectsAtPosition(worldPos);
            for (auto& object : objectsAfterRefresh) {
                if (object && object->getMapObjectPtr() && object->getMapObjectPtr()->isExitGridMarker()) {
                    object->getSprite().setColor(geck::TileColors::white());
                }
            }
            
            if (selectionManager) {
                selectionManager->clearSelection();
            }
            _editor->clearSelection();
        }
    } else {
        // Clear selection and provide user feedback
        auto* selectionManager = _editor->getSelectionManager();
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        
        // Show status message to inform user
        if (_editor->getMainWindow()) {
            _editor->getMainWindow()->showStatusMessage("No exit grids found at this position");
        }
    }
}

void ExitGridPlacementManager::selectExitGridsInArea(sf::Vector2f startPos, sf::Vector2f endPos) {
    if (!_markExitsMode || !_editor) {
        return;
    }
    
    // Create a rectangle from the two points
    sf::FloatRect selectionArea;
    selectionArea.position.x = std::min(startPos.x, endPos.x);
    selectionArea.position.y = std::min(startPos.y, endPos.y);
    selectionArea.size.x = std::abs(endPos.x - startPos.x);
    selectionArea.size.y = std::abs(endPos.y - startPos.y);
    
    // Find all exit grids in the area
    std::vector<std::shared_ptr<Object>> exitGridsInArea;
    const auto& objects = _editor->getObjects();
    
    for (auto& object : objects) {
        if (!object || !object->getMapObjectPtr() || !object->getMapObjectPtr()->isExitGridMarker()) {
            continue;
        }
        
        // Get object position
        const auto& sprite = object->getSprite();
        auto objectBounds = sprite.getGlobalBounds();
        
        // Check if object intersects with selection area
        auto intersection = selectionArea.findIntersection(objectBounds);
        if (intersection.has_value()) {
            exitGridsInArea.push_back(object);
        }
    }
    
    if (!exitGridsInArea.empty()) {
        
        // Clear current selection for visual feedback  
        auto* selectionManager = _editor->getSelectionManager();
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        
        // Also clear any drag selection preview
        _editor->clearSelection();
        
        // Show properties dialog for editing ALL exit grids
        ExitGridPropertiesDialog::ExitGridProperties newProperties;
        
        // Use the first exit grid's properties as defaults
        auto firstMapObject = exitGridsInArea[0]->getMapObjectPtr();
        if (firstMapObject) {
            ExitGridPropertiesDialog::ExitGridProperties currentProperties;
            currentProperties.exitMap = firstMapObject->exit_map;
            currentProperties.exitPosition = firstMapObject->exit_position;
            currentProperties.exitElevation = firstMapObject->exit_elevation;
            currentProperties.exitOrientation = firstMapObject->exit_orientation;
            
            if (showPropertiesDialog(newProperties, &currentProperties)) {
                // Update ALL exit grids with the new properties
                for (auto& exitGridObject : exitGridsInArea) {
                    auto mapObjectPtr = exitGridObject->getMapObjectPtr();
                    if (mapObjectPtr) {
                        mapObjectPtr->exit_map = newProperties.exitMap;
                        mapObjectPtr->exit_position = newProperties.exitPosition;
                        mapObjectPtr->exit_elevation = newProperties.exitElevation;
                        mapObjectPtr->exit_orientation = newProperties.exitOrientation;
                    }
                }
                
                // Refresh objects to show updated properties
                _editor->refreshObjects();
            }
            
            // Clear selection highlights after dialog is closed
            selectionManager->clearSelection();
            _editor->clearSelection();
        }
    } else {
        // Clear selection and provide user feedback
        auto* selectionManager = _editor->getSelectionManager();
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        
        // Show status message to inform user
        if (_editor->getMainWindow()) {
            _editor->getMainWindow()->showStatusMessage("No exit grids found in selected area");
        }
    }
}


} // namespace geck
