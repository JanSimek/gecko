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
    spdlog::debug("Exit grid placement mode {}", enabled ? "enabled" : "disabled");
}

void ExitGridPlacementManager::setMarkExitsMode(bool enabled) {
    _markExitsMode = enabled;
    spdlog::debug("Mark exits mode {}", enabled ? "enabled" : "disabled");
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

    int currentElevation = _editor->getCurrentElevation();

    // Register undo action and add to map (redo is called immediately)
    _editor->registerExitGridCreation({exitGridObject}, currentElevation);

    spdlog::info("Placed exit grid at hex position {} (elevation {})", hexPosition, currentElevation);
}

void ExitGridPlacementManager::editExitGridProperties(std::shared_ptr<MapObject> exitGrid) {
    if (!exitGrid || !exitGrid->isExitGridMarker()) {
        return;
    }

    // Capture before state for undo
    EditorWidget::ExitGridState beforeState;
    beforeState.exitMap = exitGrid->exit_map;
    beforeState.exitPosition = exitGrid->exit_position;
    beforeState.exitElevation = exitGrid->exit_elevation;
    beforeState.exitOrientation = exitGrid->exit_orientation;
    beforeState.frmPid = exitGrid->frm_pid;
    beforeState.proPid = exitGrid->pro_pid;

    // Extract current properties for dialog
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

    // Update PIDs based on exit type (world/town map vs specific map)
    bool isWorldmapExit = (newProperties.exitMap == ExitGrid::WORLD_MAP_EXIT ||
                           newProperties.exitMap == ExitGrid::TOWN_MAP_EXIT);
    if (isWorldmapExit) {
        exitGrid->pro_pid = ExitGrid::WORLD_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::WORLD_EXIT_FRM_PID;
    } else {
        exitGrid->pro_pid = ExitGrid::MAP_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::MAP_EXIT_FRM_PID;
    }

    // Capture after state for undo
    EditorWidget::ExitGridState afterState;
    afterState.exitMap = exitGrid->exit_map;
    afterState.exitPosition = exitGrid->exit_position;
    afterState.exitElevation = exitGrid->exit_elevation;
    afterState.exitOrientation = exitGrid->exit_orientation;
    afterState.frmPid = exitGrid->frm_pid;
    afterState.proPid = exitGrid->pro_pid;

    // Register undo action (changes already applied, so don't call redo)
    _editor->registerExitGridEdit({exitGrid}, {beforeState}, {afterState});

    // Refresh objects in editor to show updated properties and FRM
    // FIXME: Changing exit grid FRM may cause visual position offset due to different FRM shift values
    _editor->refreshObjects();

    spdlog::info("Updated exit grid: map={}, frm_pid=0x{:08X}",
        newProperties.exitMap, exitGrid->frm_pid);
}

std::shared_ptr<MapObject> ExitGridPlacementManager::createExitGridObject(int hexPosition, const ExitGridPropertiesDialog::ExitGridProperties& properties) {
    auto exitGrid = std::make_shared<MapObject>();

    // Set basic object properties
    exitGrid->unknown0 = 0;
    exitGrid->position = hexPosition;
    exitGrid->x = 0; // These are calculated from position
    exitGrid->y = 0;
    exitGrid->sx = 0;
    exitGrid->sy = 0;
    exitGrid->frame_number = 0;
    exitGrid->direction = 0;

    // Set exit grid PIDs based on exit type
    // World/town map exit: Uses WORLD_EXIT_PRO_PID/WORLD_EXIT_FRM_PID
    // Map exit: Uses MAP_EXIT_PRO_PID/MAP_EXIT_FRM_PID
    bool isWorldmapExit = (properties.exitMap == ExitGrid::WORLD_MAP_EXIT ||
                           properties.exitMap == ExitGrid::TOWN_MAP_EXIT);
    if (isWorldmapExit) {
        exitGrid->pro_pid = ExitGrid::WORLD_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::WORLD_EXIT_FRM_PID;
    } else {
        exitGrid->pro_pid = ExitGrid::MAP_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::MAP_EXIT_FRM_PID;
    }
    exitGrid->flags = 0;
    exitGrid->elevation = _editor->getCurrentElevation();

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
                // Capture before states for undo
                std::vector<std::shared_ptr<MapObject>> mapObjects;
                std::vector<EditorWidget::ExitGridState> beforeStates;
                for (auto& exitGridObject : exitGridsInArea) {
                    auto mapObjectPtr = exitGridObject->getMapObjectPtr();
                    if (mapObjectPtr) {
                        mapObjects.push_back(mapObjectPtr);
                        EditorWidget::ExitGridState beforeState;
                        beforeState.exitMap = mapObjectPtr->exit_map;
                        beforeState.exitPosition = mapObjectPtr->exit_position;
                        beforeState.exitElevation = mapObjectPtr->exit_elevation;
                        beforeState.exitOrientation = mapObjectPtr->exit_orientation;
                        beforeState.frmPid = mapObjectPtr->frm_pid;
                        beforeState.proPid = mapObjectPtr->pro_pid;
                        beforeStates.push_back(beforeState);
                    }
                }

                // Determine PIDs based on exit type
                bool isWorldmapExit = (newProperties.exitMap == ExitGrid::WORLD_MAP_EXIT ||
                                       newProperties.exitMap == ExitGrid::TOWN_MAP_EXIT);
                uint32_t newProPid = isWorldmapExit ? ExitGrid::WORLD_EXIT_PRO_PID : ExitGrid::MAP_EXIT_PRO_PID;
                uint32_t newFrmPid = isWorldmapExit ? ExitGrid::WORLD_EXIT_FRM_PID : ExitGrid::MAP_EXIT_FRM_PID;

                // Update ALL exit grids with the new properties
                std::vector<EditorWidget::ExitGridState> afterStates;
                for (auto& mapObjectPtr : mapObjects) {
                    mapObjectPtr->exit_map = newProperties.exitMap;
                    mapObjectPtr->exit_position = newProperties.exitPosition;
                    mapObjectPtr->exit_elevation = newProperties.exitElevation;
                    mapObjectPtr->exit_orientation = newProperties.exitOrientation;
                    mapObjectPtr->pro_pid = newProPid;
                    mapObjectPtr->frm_pid = newFrmPid;

                    EditorWidget::ExitGridState afterState;
                    afterState.exitMap = mapObjectPtr->exit_map;
                    afterState.exitPosition = mapObjectPtr->exit_position;
                    afterState.exitElevation = mapObjectPtr->exit_elevation;
                    afterState.exitOrientation = mapObjectPtr->exit_orientation;
                    afterState.frmPid = mapObjectPtr->frm_pid;
                    afterState.proPid = mapObjectPtr->pro_pid;
                    afterStates.push_back(afterState);
                }

                // Register undo action
                _editor->registerExitGridEdit(mapObjects, beforeStates, afterStates);

                spdlog::info("Updated {} exit grids: map={}, frm_pid=0x{:08X}",
                    exitGridsInArea.size(), newProperties.exitMap, newFrmPid);

                // Refresh objects to show updated properties
                // FIXME: Changing exit grid FRM may cause visual position offset due to different FRM shift values
                _editor->refreshObjects();
            }

            // Clear selection highlights after dialog is closed
            selectionManager->clearSelection();
            _editor->clearSelection();
        }
    } else {
        // No existing exit grids - create new ones in the selected area
        auto* selectionManager = _editor->getSelectionManager();
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        _editor->clearSelection();

        // Get all hexes in the selected area
        std::vector<int> hexesInArea;
        auto* hexGrid = _editor->getHexagonGrid();
        if (hexGrid) {
            for (int hexIndex = 0; hexIndex < (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT); ++hexIndex) {
                if (hexIndex < static_cast<int>(hexGrid->grid().size())) {
                    const auto& hex = hexGrid->grid().at(hexIndex);
                    sf::Vector2f hexPos(static_cast<float>(hex.x()), static_cast<float>(hex.y()));

                    // Check if hex center is within selection area
                    sf::FloatRect hexBounds(sf::Vector2f(hexPos.x - 16, hexPos.y - 8), sf::Vector2f(32, 16));
                    if (selectionArea.findIntersection(hexBounds)) {
                        hexesInArea.push_back(hexIndex);
                    }
                }
            }
        }

        if (hexesInArea.empty()) {
            if (_editor->getMainWindow()) {
                _editor->getMainWindow()->showStatusMessage("No hexes found in selected area");
            }
            return;
        }

        // Show properties dialog for new exit grids
        ExitGridPropertiesDialog::ExitGridProperties newProperties;
        if (!showPropertiesDialog(newProperties)) {
            // User cancelled
            return;
        }

        // Create exit grid objects for all hexes in the area
        int currentElevation = _editor->getCurrentElevation();
        std::vector<std::shared_ptr<MapObject>> createdExitGrids;

        for (int hexPosition : hexesInArea) {
            auto exitGrid = createExitGridObject(hexPosition, newProperties);
            if (exitGrid) {
                createdExitGrids.push_back(exitGrid);
            }
        }

        if (!createdExitGrids.empty()) {
            // Register undo action and add to map (redo is called immediately)
            _editor->registerExitGridCreation(createdExitGrids, currentElevation);

            spdlog::info("Created {} exit grids in selected area (elevation {})",
                createdExitGrids.size(), currentElevation);

            if (_editor->getMainWindow()) {
                _editor->getMainWindow()->showStatusMessage(
                    QString("Created %1 exit grids").arg(createdExitGrids.size()));
            }
        }
    }
}

} // namespace geck
