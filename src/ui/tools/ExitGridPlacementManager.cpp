#include "ExitGridPlacementManager.h"
#include "ExitGridContext.h"
#include "ui/widgets/SFMLWidget.h"
#include "viewport/ViewportController.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "util/Coordinates.h"
#include "util/Constants.h"
#include "util/PolygonGeometry.h"
#include "util/TileUtils.h"
#include "editor/HexagonGrid.h"
#include "selection/SelectionManager.h"
#include "selection/SelectionState.h"
#include "resource/MapNameResolver.h"

#include <QApplication>
#include <spdlog/spdlog.h>

namespace geck {

ExitGridPlacementManager::ExitGridPlacementManager(ExitGridContext& context, resource::GameResources& resources,
    std::function<void(const QString&)> showStatus)
    : _context(context)
    , _resources(resources)
    , _showStatus(std::move(showStatus)) {
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
    if (!_context.getMap()) {
        spdlog::error("Cannot place exit grid: no map available");
        return;
    }

    auto hexPositionOpt = _context.getViewportController()->worldPosToHexPosition(WorldCoords(worldPos.x, worldPos.y));
    if (!hexPositionOpt.has_value() || !hexPositionOpt->isValid()) {
        spdlog::error("Invalid hex position for exit grid placement");
        return;
    }
    int hexPosition = hexPositionOpt->value();

    ExitGridPropertiesDialog::ExitGridProperties properties;
    if (!showPropertiesDialog(properties)) {
        return; // User cancelled
    }

    auto exitGridObject = createExitGridObject(hexPosition, properties);
    int currentElevation = _context.getCurrentElevation();

    // Registers the undo action and adds to map; redo runs immediately
    _context.registerExitGridCreation({ exitGridObject }, currentElevation);

    spdlog::info("Placed exit grid at hex position {} (elevation {})", hexPosition, currentElevation);
}

void ExitGridPlacementManager::editExitGridProperties(std::shared_ptr<MapObject> exitGrid) {
    if (!exitGrid || !exitGrid->isExitGridMarker()) {
        return;
    }

    // Capture before state for undo
    ExitGridContext::ExitGridState beforeState;
    beforeState.exitMap = exitGrid->exit_map;
    beforeState.exitPosition = exitGrid->exit_position;
    beforeState.exitElevation = exitGrid->exit_elevation;
    beforeState.exitOrientation = exitGrid->exit_orientation;
    beforeState.frmPid = exitGrid->frm_pid;
    beforeState.proPid = exitGrid->pro_pid;

    ExitGridPropertiesDialog::ExitGridProperties currentProperties;
    currentProperties.exitMap = exitGrid->exit_map;
    currentProperties.exitPosition = exitGrid->exit_position;
    currentProperties.exitElevation = exitGrid->exit_elevation;
    currentProperties.exitOrientation = exitGrid->exit_orientation;

    ExitGridPropertiesDialog::ExitGridProperties newProperties;
    if (!showPropertiesDialog(newProperties, &currentProperties)) {
        return; // User cancelled
    }

    exitGrid->exit_map = newProperties.exitMap;
    exitGrid->exit_position = newProperties.exitPosition;
    exitGrid->exit_elevation = newProperties.exitElevation;
    exitGrid->exit_orientation = newProperties.exitOrientation;

    // PIDs depend on exit type (world/town map vs specific map)
    bool isWorldmapExit = (newProperties.exitMap == ExitGrid::WORLD_MAP_EXIT || newProperties.exitMap == ExitGrid::TOWN_MAP_EXIT);
    if (isWorldmapExit) {
        exitGrid->pro_pid = ExitGrid::WORLD_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::WORLD_EXIT_FRM_PID;
    } else {
        exitGrid->pro_pid = ExitGrid::MAP_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::MAP_EXIT_FRM_PID;
    }

    // Capture after state for undo
    ExitGridContext::ExitGridState afterState;
    afterState.exitMap = exitGrid->exit_map;
    afterState.exitPosition = exitGrid->exit_position;
    afterState.exitElevation = exitGrid->exit_elevation;
    afterState.exitOrientation = exitGrid->exit_orientation;
    afterState.frmPid = exitGrid->frm_pid;
    afterState.proPid = exitGrid->pro_pid;

    // Changes already applied, so register undo without calling redo
    _context.registerExitGridEdit({ exitGrid }, { beforeState }, { afterState });

    // FIXME: Changing exit grid FRM may cause visual position offset due to different FRM shift values
    _context.refreshObjects();

    spdlog::info("Updated exit grid: map={}, frm_pid=0x{:08X}",
        newProperties.exitMap, exitGrid->frm_pid);
}

std::shared_ptr<MapObject> ExitGridPlacementManager::createExitGridObject(int hexPosition, const ExitGridPropertiesDialog::ExitGridProperties& properties) {
    auto exitGrid = std::make_shared<MapObject>();

    exitGrid->unknown0 = 0;
    exitGrid->position = hexPosition;
    exitGrid->x = 0; // x/y are derived from position
    exitGrid->y = 0;
    exitGrid->sx = 0;
    exitGrid->sy = 0;
    exitGrid->frame_number = 0;
    exitGrid->direction = 0;

    // Set exit grid PIDs based on exit type
    // World/town map exit: Uses WORLD_EXIT_PRO_PID/WORLD_EXIT_FRM_PID
    // Map exit: Uses MAP_EXIT_PRO_PID/MAP_EXIT_FRM_PID
    bool isWorldmapExit = (properties.exitMap == ExitGrid::WORLD_MAP_EXIT || properties.exitMap == ExitGrid::TOWN_MAP_EXIT);
    if (isWorldmapExit) {
        exitGrid->pro_pid = ExitGrid::WORLD_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::WORLD_EXIT_FRM_PID;
    } else {
        exitGrid->pro_pid = ExitGrid::MAP_EXIT_PRO_PID;
        exitGrid->frm_pid = ExitGrid::MAP_EXIT_FRM_PID;
    }
    exitGrid->flags = 0;
    exitGrid->elevation = _context.getCurrentElevation();

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

    exitGrid->exit_map = properties.exitMap;
    exitGrid->exit_position = properties.exitPosition;
    exitGrid->exit_elevation = properties.exitElevation;
    exitGrid->exit_orientation = properties.exitOrientation;

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
    // maps.txt is small; build the resolver per dialog so it reflects the currently-mounted data.
    const resource::MapNameResolver mapNames(_resources);
    ExitGridPropertiesDialog dialog(existing ? *existing : ExitGridPropertiesDialog::ExitGridProperties{},
        _context.getDialogParent(), &mapNames);

    if (!existing) {
        ExitGridPropertiesDialog::ExitGridProperties defaults;
        defaults.exitMap = 0;
        defaults.exitPosition = 0;
        defaults.exitElevation = 0;
        defaults.exitOrientation = 0;
        dialog.setProperties(defaults);
    }

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
    auto* selectionManager = _context.getSelectionManager();
    if (!selectionManager) {
        return false;
    }

    const auto& selectionState = selectionManager->getCurrentSelection();
    if (selectionState.isEmpty()) {
        return false;
    }

    // Edit the first selected exit grid
    for (const auto& item : selectionState.items) {
        if (item.isObject()) {
            auto selectedObject = item.getObject();
            if (!selectedObject) {
                continue;
            }

            auto& mapObject = selectedObject->getMapObject();

            if (mapObject.isExitGridMarker()) {
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
    if (!_markExitsMode) {
        return;
    }

    auto objects = _context.getObjectsAtPosition(worldPos);
    std::vector<std::shared_ptr<Object>> exitGrids;

    for (auto& object : objects) {
        if (object && object->getMapObjectPtr() && object->getMapObjectPtr()->isExitGridMarker()) {
            exitGrids.push_back(object);
        }
    }

    if (!exitGrids.empty()) {

        auto* selectionManager = _context.getSelectionManager();
        if (selectionManager) {
            selectionManager->clearSelection();
        }

        _context.clearSelection();

        // Highlight the selected exit grids BEFORE opening the dialog
        for (auto& exitGrid : exitGrids) {
            exitGrid->getSprite().setColor(geck::TileColors::exitGridHighlight());
        }

        // Force an immediate SFML render so the highlight shows before the dialog opens
        if (auto* sfmlWidget = _context.getSFMLWidget()) {
            sfmlWidget->updateAndRender();
        }
        QApplication::processEvents();

        auto firstExitGrid = exitGrids[0];
        auto mapObjectPtr = firstExitGrid->getMapObjectPtr();
        if (mapObjectPtr) {
            editExitGridProperties(mapObjectPtr);

            // After the dialog closes, clear the highlighting.
            // refreshObjects() may have recreated objects, so look them up again.
            auto objectsAfterRefresh = _context.getObjectsAtPosition(worldPos);
            for (auto& object : objectsAfterRefresh) {
                if (object && object->getMapObjectPtr() && object->getMapObjectPtr()->isExitGridMarker()) {
                    object->getSprite().setColor(geck::TileColors::white());
                }
            }

            if (selectionManager) {
                selectionManager->clearSelection();
            }
            _context.clearSelection();
        }
    } else {
        auto* selectionManager = _context.getSelectionManager();
        if (selectionManager) {
            selectionManager->clearSelection();
        }

        _showStatus("No exit grids found at this position");
    }
}

void ExitGridPlacementManager::rememberDestinationKind(uint32_t exitMap) {
    const bool isWorldmapExit = (exitMap == ExitGrid::WORLD_MAP_EXIT || exitMap == ExitGrid::TOWN_MAP_EXIT);
    _currentDestinationKind = isWorldmapExit ? DestinationKind::WorldMap : DestinationKind::InterMap;
}

bool ExitGridPlacementManager::bulkEditExistingExitGrids(const std::vector<std::shared_ptr<Object>>& exitGrids) {
    auto firstMapObject = exitGrids.empty() ? nullptr : exitGrids.front()->getMapObjectPtr();
    if (!firstMapObject) {
        return false;
    }

    ExitGridPropertiesDialog::ExitGridProperties currentProperties;
    currentProperties.exitMap = firstMapObject->exit_map;
    currentProperties.exitPosition = firstMapObject->exit_position;
    currentProperties.exitElevation = firstMapObject->exit_elevation;
    currentProperties.exitOrientation = firstMapObject->exit_orientation;

    ExitGridPropertiesDialog::ExitGridProperties newProperties;
    if (!showPropertiesDialog(newProperties, &currentProperties)) {
        return false; // User cancelled
    }
    rememberDestinationKind(newProperties.exitMap);

    // Capture before states for undo
    std::vector<std::shared_ptr<MapObject>> mapObjects;
    std::vector<ExitGridContext::ExitGridState> beforeStates;
    for (auto& exitGridObject : exitGrids) {
        auto mapObjectPtr = exitGridObject->getMapObjectPtr();
        if (!mapObjectPtr) {
            continue;
        }
        mapObjects.push_back(mapObjectPtr);
        ExitGridContext::ExitGridState beforeState;
        beforeState.exitMap = mapObjectPtr->exit_map;
        beforeState.exitPosition = mapObjectPtr->exit_position;
        beforeState.exitElevation = mapObjectPtr->exit_elevation;
        beforeState.exitOrientation = mapObjectPtr->exit_orientation;
        beforeState.frmPid = mapObjectPtr->frm_pid;
        beforeState.proPid = mapObjectPtr->pro_pid;
        beforeStates.push_back(beforeState);
    }

    // PIDs depend on exit type (world/town map vs specific map)
    bool isWorldmapExit = (newProperties.exitMap == ExitGrid::WORLD_MAP_EXIT || newProperties.exitMap == ExitGrid::TOWN_MAP_EXIT);
    uint32_t newProPid = isWorldmapExit ? ExitGrid::WORLD_EXIT_PRO_PID : ExitGrid::MAP_EXIT_PRO_PID;
    uint32_t newFrmPid = isWorldmapExit ? ExitGrid::WORLD_EXIT_FRM_PID : ExitGrid::MAP_EXIT_FRM_PID;

    std::vector<ExitGridContext::ExitGridState> afterStates;
    for (auto& mapObjectPtr : mapObjects) {
        mapObjectPtr->exit_map = newProperties.exitMap;
        mapObjectPtr->exit_position = newProperties.exitPosition;
        mapObjectPtr->exit_elevation = newProperties.exitElevation;
        mapObjectPtr->exit_orientation = newProperties.exitOrientation;
        mapObjectPtr->pro_pid = newProPid;
        mapObjectPtr->frm_pid = newFrmPid;

        ExitGridContext::ExitGridState afterState;
        afterState.exitMap = mapObjectPtr->exit_map;
        afterState.exitPosition = mapObjectPtr->exit_position;
        afterState.exitElevation = mapObjectPtr->exit_elevation;
        afterState.exitOrientation = mapObjectPtr->exit_orientation;
        afterState.frmPid = mapObjectPtr->frm_pid;
        afterState.proPid = mapObjectPtr->pro_pid;
        afterStates.push_back(afterState);
    }

    _context.registerExitGridEdit(mapObjects, beforeStates, afterStates);

    spdlog::info("Updated {} exit grids: map={}, frm_pid=0x{:08X}",
        mapObjects.size(), newProperties.exitMap, newFrmPid);

    // FIXME: Changing exit grid FRM may cause visual position offset due to different FRM shift values
    _context.refreshObjects();
    return true;
}

std::size_t ExitGridPlacementManager::createExitGridsForHexes(const std::vector<int>& hexPositions) {
    if (hexPositions.empty()) {
        _showStatus("No hexes found in selected area");
        return 0;
    }

    ExitGridPropertiesDialog::ExitGridProperties newProperties;
    if (!showPropertiesDialog(newProperties)) {
        return 0; // User cancelled
    }
    rememberDestinationKind(newProperties.exitMap);

    int currentElevation = _context.getCurrentElevation();
    std::vector<std::shared_ptr<MapObject>> createdExitGrids;
    for (int hexPosition : hexPositions) {
        createdExitGrids.push_back(createExitGridObject(hexPosition, newProperties));
    }

    if (createdExitGrids.empty()) {
        return 0;
    }

    // Registers the undo action and adds to map; redo runs immediately
    _context.registerExitGridCreation(createdExitGrids, currentElevation);

    spdlog::info("Created {} exit grids in selected area (elevation {})",
        createdExitGrids.size(), currentElevation);
    _showStatus(QString("Created %1 exit grids").arg(createdExitGrids.size()));
    return createdExitGrids.size();
}

void ExitGridPlacementManager::selectExitGridsInArea(sf::Vector2f startPos, sf::Vector2f endPos) {
    if (!_markExitsMode) {
        return;
    }

    sf::FloatRect selectionArea;
    selectionArea.position.x = std::min(startPos.x, endPos.x);
    selectionArea.position.y = std::min(startPos.y, endPos.y);
    selectionArea.size.x = std::abs(endPos.x - startPos.x);
    selectionArea.size.y = std::abs(endPos.y - startPos.y);

    std::vector<std::shared_ptr<Object>> exitGridsInArea;
    for (auto& object : _context.getObjects()) {
        if (!object || !object->getMapObjectPtr() || !object->getMapObjectPtr()->isExitGridMarker()) {
            continue;
        }
        if (selectionArea.findIntersection(object->getSprite().getGlobalBounds()).has_value()) {
            exitGridsInArea.push_back(object);
        }
    }

    auto* selectionManager = _context.getSelectionManager();
    if (selectionManager) {
        selectionManager->clearSelection();
    }
    _context.clearSelection();

    if (!exitGridsInArea.empty()) {
        bulkEditExistingExitGrids(exitGridsInArea);
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        _context.clearSelection();
        return;
    }

    // No existing exit grids - create new ones on every hex whose footprint the rectangle covers.
    std::vector<int> hexesInArea;
    if (const auto* hexGrid = _context.getHexagonGrid(); hexGrid) {
        for (int hexIndex = 0; hexIndex < static_cast<int>(hexGrid->size()); ++hexIndex) {
            auto hex = hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex));
            if (!hex.has_value()) {
                continue;
            }
            sf::Vector2f hexPos(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
            // 32x16 hex footprint centered on the hex position
            sf::FloatRect hexBounds(sf::Vector2f(hexPos.x - 16, hexPos.y - 8), sf::Vector2f(32, 16));
            if (selectionArea.findIntersection(hexBounds)) {
                hexesInArea.push_back(hexIndex);
            }
        }
    }
    createExitGridsForHexes(hexesInArea);
}

std::vector<std::shared_ptr<Object>> ExitGridPlacementManager::collectExitGridsInPolygon(
    const std::vector<sf::Vector2f>& worldVertices) const {
    std::vector<std::shared_ptr<Object>> result;
    const auto* hexGrid = _context.getHexagonGrid();
    if (!hexGrid) {
        return result;
    }
    const auto bounds = geometry::polygonBounds(worldVertices);
    for (auto& object : _context.getObjects()) {
        auto mapObjectPtr = object ? object->getMapObjectPtr() : nullptr;
        if (!mapObjectPtr || !mapObjectPtr->isExitGridMarker()) {
            continue;
        }
        auto hex = hexGrid->getHexByPosition(static_cast<uint32_t>(mapObjectPtr->position));
        if (!hex.has_value()) {
            continue;
        }
        sf::Vector2f center(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
        if (bounds.contains(center) && geometry::pointInPolygon(center, worldVertices)) {
            result.push_back(object);
        }
    }
    return result;
}

std::vector<int> ExitGridPlacementManager::collectHexesInPolygon(
    const std::vector<sf::Vector2f>& worldVertices) const {
    std::vector<int> result;
    const auto* hexGrid = _context.getHexagonGrid();
    if (!hexGrid) {
        return result;
    }
    const auto bounds = geometry::polygonBounds(worldVertices);
    for (int hexIndex = 0; hexIndex < static_cast<int>(hexGrid->size()); ++hexIndex) {
        auto hex = hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex));
        if (!hex.has_value()) {
            continue;
        }
        sf::Vector2f center(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
        if (bounds.contains(center) && geometry::pointInPolygon(center, worldVertices)) {
            result.push_back(hexIndex);
        }
    }
    return result;
}

void ExitGridPlacementManager::selectExitGridsInPolygon(const std::vector<sf::Vector2f>& worldVertices) {
    if (!_markExitsMode || worldVertices.size() < 3) {
        return;
    }

    auto* selectionManager = _context.getSelectionManager();
    if (selectionManager) {
        selectionManager->clearSelection();
    }
    _context.clearSelection();

    // Existing exit grids inside the polygon -> bulk-edit them; otherwise create one per interior hex.
    if (const auto existing = collectExitGridsInPolygon(worldVertices); !existing.empty()) {
        bulkEditExistingExitGrids(existing);
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        _context.clearSelection();
        return;
    }

    createExitGridsForHexes(collectHexesInPolygon(worldVertices));
}

} // namespace geck
