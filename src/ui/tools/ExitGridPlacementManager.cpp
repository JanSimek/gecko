#include "ExitGridPlacementManager.h"
#include "ExitGridContext.h"
#include "ui/widgets/SFMLWidget.h"
#include "viewport/ViewportController.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "util/Coordinates.h"
#include "util/Constants.h"
#include "util/ExitGridDirection.h"
#include "util/HexLine.h"
#include "util/TileUtils.h"
#include "editor/HexagonGrid.h"
#include "selection/SelectionManager.h"
#include "selection/SelectionState.h"
#include "resource/MapNameResolver.h"

#include <QApplication>
#include <set>
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
    _currentMarkerArt = properties.markerArt;

    const ExitGridArt art = artForHex(hexPosition, properties.markerArt);
    auto exitGridObject = createExitGridObject(hexPosition, art.proPid, art.frmPid, properties);
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

std::shared_ptr<MapObject> ExitGridPlacementManager::createExitGridObject(int hexPosition,
    uint32_t proPid, uint32_t frmPid, const ExitGridPropertiesDialog::ExitGridProperties& properties) {
    auto exitGrid = std::make_shared<MapObject>();

    exitGrid->unknown0 = 0;
    exitGrid->position = hexPosition;
    exitGrid->x = 0; // x/y are derived from position
    exitGrid->y = 0;
    exitGrid->sx = 0;
    exitGrid->sy = 0;
    exitGrid->frame_number = 0;
    exitGrid->direction = 0;

    // The exit-grid art is direction-specific (which way the exit faces), chosen per hex by the
    // caller from its outward facing — independent of the destination, which drives only the exit_*
    // fields below.
    exitGrid->pro_pid = proPid;
    exitGrid->frm_pid = frmPid;
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

namespace {
    // The fixed directional art for an explicit (non-Auto) side override.
    ExitGridArt explicitSideArt(ExitGridPropertiesDialog::ExitGridProperties::MarkerArt markerArt) {
        using MarkerArt = ExitGridPropertiesDialog::ExitGridProperties::MarkerArt;
        switch (markerArt) {
            case MarkerArt::Left:
                return { ExitGrid::RECT_LEFT_PRO_PID, ExitGrid::RECT_LEFT_FRM_PID };
            case MarkerArt::Right:
                return { ExitGrid::RECT_RIGHT_PRO_PID, ExitGrid::RECT_RIGHT_FRM_PID };
            case MarkerArt::Top:
                return { ExitGrid::RECT_TOP_PRO_PID, ExitGrid::RECT_TOP_FRM_PID };
            case MarkerArt::Bottom:
                return { ExitGrid::RECT_BOTTOM_PRO_PID, ExitGrid::RECT_BOTTOM_FRM_PID };
            case MarkerArt::Auto:
                break;
        }
        return { ExitGrid::MAP_EXIT_PRO_PID, ExitGrid::MAP_EXIT_FRM_PID };
    }
} // namespace

ExitGridArt ExitGridPlacementManager::directionalArtForHex(int hexPosition) const {
    const auto* hexGrid = _context.getHexagonGrid();
    const ExitGridArt mapExit{ ExitGrid::MAP_EXIT_PRO_PID, ExitGrid::MAP_EXIT_FRM_PID };
    if (!hexGrid) {
        return mapExit;
    }
    const auto hex = hexGrid->getHexByPosition(static_cast<uint32_t>(hexPosition));
    if (!hex.has_value()) {
        return mapExit;
    }
    const auto [centerX, centerY] = hexGridCenterScreen(*hexGrid);
    return exitGridArtForFacing(hex->get().x(), hex->get().y(), centerX, centerY);
}

ExitGridArt ExitGridPlacementManager::artForHex(int hexPosition,
    ExitGridPropertiesDialog::ExitGridProperties::MarkerArt markerArt) const {
    using MarkerArt = ExitGridPropertiesDialog::ExitGridProperties::MarkerArt;
    // Auto keeps the per-hex outward-facing classification; an explicit side forces one art for
    // every hex in the region (the escape hatch for ambiguous corners).
    if (markerArt == MarkerArt::Auto) {
        return directionalArtForHex(hexPosition);
    }
    return explicitSideArt(markerArt);
}

uint32_t ExitGridPlacementManager::previewFrmPidForHex(int hexPosition) const {
    return artForHex(hexPosition, _currentMarkerArt).frmPid;
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
        _showStatus("No hexes found along the edge line");
        return 0;
    }

    ExitGridPropertiesDialog::ExitGridProperties newProperties;
    if (!showPropertiesDialog(newProperties)) {
        return 0; // User cancelled
    }
    rememberDestinationKind(newProperties.exitMap);
    _currentMarkerArt = newProperties.markerArt;

    int currentElevation = _context.getCurrentElevation();
    std::vector<std::shared_ptr<MapObject>> createdExitGrids;
    for (int hexPosition : hexPositions) {
        const ExitGridArt art = artForHex(hexPosition, newProperties.markerArt);
        createdExitGrids.push_back(createExitGridObject(hexPosition, art.proPid, art.frmPid, newProperties));
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

std::vector<int> ExitGridPlacementManager::collectHexesAlongLine(
    const std::vector<sf::Vector2f>& worldVertices) const {
    const auto* hexGrid = _context.getHexagonGrid();
    auto* viewport = _context.getViewportController();
    if (!hexGrid || !viewport || worldVertices.size() < 2) {
        return {};
    }

    // Map each polyline vertex to its hex (-1 if off-grid), then join consecutive hexes by a
    // gap-free hex-line walk (the iso staircase), deduping shared corners.
    std::vector<int> vertexHexes;
    vertexHexes.reserve(worldVertices.size());
    for (const sf::Vector2f& vertex : worldVertices) {
        const auto hexOpt = viewport->worldPosToHexPosition(WorldCoords(vertex.x, vertex.y));
        vertexHexes.push_back((hexOpt.has_value() && hexOpt->isValid()) ? hexOpt->value() : -1);
    }
    return hexline::hexPolyline(*hexGrid, vertexHexes);
}

std::vector<std::shared_ptr<Object>> ExitGridPlacementManager::collectExitGridsOnHexes(
    const std::vector<int>& hexPositions) const {
    std::vector<std::shared_ptr<Object>> result;
    if (hexPositions.empty()) {
        return result;
    }
    const std::set<int> wanted(hexPositions.begin(), hexPositions.end());
    for (auto& object : _context.getObjects()) {
        auto mapObjectPtr = object ? object->getMapObjectPtr() : nullptr;
        if (!mapObjectPtr || !mapObjectPtr->isExitGridMarker()) {
            continue;
        }
        if (wanted.count(static_cast<int>(mapObjectPtr->position)) > 0) {
            result.push_back(object);
        }
    }
    return result;
}

void ExitGridPlacementManager::selectExitGridsAlongLine(const std::vector<sf::Vector2f>& worldVertices) {
    if (!_markExitsMode || worldVertices.size() < 2) {
        return;
    }

    auto* selectionManager = _context.getSelectionManager();
    if (selectionManager) {
        selectionManager->clearSelection();
    }
    _context.clearSelection();

    const std::vector<int> lineHexes = collectHexesAlongLine(worldVertices);

    // Existing exit grids on the line -> bulk-edit them; otherwise create one per line hex, each
    // with the directional art for its outward facing.
    if (const auto existing = collectExitGridsOnHexes(lineHexes); !existing.empty()) {
        bulkEditExistingExitGrids(existing);
        if (selectionManager) {
            selectionManager->clearSelection();
        }
        _context.clearSelection();
        return;
    }

    createExitGridsForHexes(lineHexes);
}

} // namespace geck
