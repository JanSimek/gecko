#include "ExitGridPlacementManager.h"
#include "../core/EditorWidget.h"
#include "../viewport/ViewportController.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapObject.h"
#include "../../util/Coordinates.h"
#include "../../util/Constants.h"
#include "../../editor/HexagonGrid.h"

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
    ExitGridPropertiesDialog* dialog;
    
    if (existing) {
        // Use constructor that takes properties directly
        dialog = new ExitGridPropertiesDialog(*existing, _editor);
    } else {
        // For new exit grids, use default constructor and initialize defaults
        dialog = new ExitGridPropertiesDialog(_editor);
        ExitGridPropertiesDialog::ExitGridProperties defaults;
        defaults.exitMap = 0;
        defaults.exitPosition = 0;
        defaults.exitElevation = 0;
        defaults.exitOrientation = 0;
        dialog->setProperties(defaults);
    }
    
    bool result = false;
    if (dialog->exec() == QDialog::Accepted) {
        properties = dialog->getProperties();
        result = true;
    }
    
    delete dialog;
    return result;
}

uint32_t ExitGridPlacementManager::getAvailableExitGridPID() const {
    // Exit grids use MISC type (0x05) with indices 16-23
    // For now, just use the first one (16)
    // TODO: Could check existing objects to find first available slot
    return 0x05000010; // MISC type | index 16
}

void ExitGridPlacementManager::resetState() {
    _exitGridPlacementMode = false;
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

} // namespace geck