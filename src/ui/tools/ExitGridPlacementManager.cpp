#include "ExitGridPlacementManager.h"
#include "ExitGridContext.h"
#include "viewport/ViewportController.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "util/Coordinates.h"
#include "util/Constants.h"
#include "util/ExitGridDirection.h"
#include "util/HexLine.h"
#include "editor/HexagonGrid.h"
#include "selection/SelectionManager.h"
#include "selection/SelectionState.h"
#include "resource/MapNameResolver.h"

#include <set>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>

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
    rememberDestinationKind(properties.exitMap);
    _currentMarkerArt = properties.markerArt;

    // A single hex has no stroke: the art falls back to the hex's center-facing classification (or the
    // explicit override, if chosen). No flip on a click-placed lone marker.
    const ExitGridArt art = artForLine({ hexPosition }, properties.markerArt, /*flipSide=*/false);
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

    // A destination-only edit: update only the exit_* fields and keep the existing directional art
    // (pro_pid/frm_pid). Overwriting the art here would collapse every edited marker to one fixed
    // direction and lose the per-hex art the line tool placed.
    exitGrid->exit_map = newProperties.exitMap;
    exitGrid->exit_position = newProperties.exitPosition;
    exitGrid->exit_elevation = newProperties.exitElevation;
    exitGrid->exit_orientation = newProperties.exitOrientation;

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
    using MarkerArt = ExitGridPropertiesDialog::ExitGridProperties::MarkerArt;

    // The explicit MarkerArt directions map 1:1 onto ExitGrid::Direction (0..7): Auto is the only
    // value with no fixed direction. -1 means "no explicit direction" (i.e. Auto).
    int explicitDirection(MarkerArt markerArt) {
        using enum MarkerArt;
        switch (markerArt) {
            case Left:
                return ExitGrid::DIR_LEFT;
            case Right:
                return ExitGrid::DIR_RIGHT;
            case Bottom:
                return ExitGrid::DIR_BOTTOM;
            case Top:
                return ExitGrid::DIR_TOP;
            case ForwardA:
                return ExitGrid::DIR_FWD_A;
            case ForwardB:
                return ExitGrid::DIR_FWD_B;
            case BackA:
                return ExitGrid::DIR_BACK_A;
            case BackB:
                return ExitGrid::DIR_BACK_B;
            case Auto:
                break;
        }
        return -1;
    }
} // namespace

ExitGridDestinationKind ExitGridPlacementManager::destinationKind() const {
    return _currentDestinationKind == DestinationKind::WorldMap
        ? ExitGridDestinationKind::WorldMap
        : ExitGridDestinationKind::InterMap;
}

ExitGridArt ExitGridPlacementManager::autoArtForLine(const std::vector<int>& orderedHexes,
    bool flipSide) const {
    const auto* hexGrid = _context.getHexagonGrid();
    const ExitGridDestinationKind kind = destinationKind();
    // Off-grid / empty fallback: a deterministic bottom-edge marker in the right family.
    if (!hexGrid || orderedHexes.empty()) {
        return exitGridArtForDirection(ExitGrid::DIR_BOTTOM, kind);
    }
    const auto screenOf = [hexGrid](int hexIndex) -> std::pair<int, int> {
        const auto h = hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex));
        return h.has_value() ? std::pair<int, int>{ h->get().x(), h->get().y() }
                             : std::pair<int, int>{ 0, 0 };
    };
    const auto [centerX, centerY] = hexGridCenterScreen(*hexGrid);

    // A lone hex (no stroke) has no axis: classify it purely by its outward facing.
    if (orderedHexes.size() < 2) {
        const auto [hx, hy] = screenOf(orderedHexes.front());
        const ExitGridArt art = exitGridArtForFacing(hx, hy, centerX, centerY, kind);
        // Honour a flip even for a lone hex so the preview/commit stays consistent.
        if (flipSide) {
            return exitGridArtForDirection(
                flipExitGridDirection(static_cast<int>(art.proPid - ExitGrid::FIRST_EXIT_GRID_PID)),
                kind);
        }
        return art;
    }

    // Whole-stroke classification: the screen delta from the first to the last hex picks the axis, and
    // the stroke midpoint's offset from the map centre picks the side — ONCE for the whole edge, so
    // every hex shares one consistent side (a clean continuous bar) instead of flipping mid-run.
    const auto [fx, fy] = screenOf(orderedHexes.front());
    const auto [lx, ly] = screenOf(orderedHexes.back());
    const auto [mx, my] = screenOf(orderedHexes[orderedHexes.size() / 2]);
    const int dir = exitGridDirectionForLine(lx - fx, ly - fy, mx - centerX, my - centerY, flipSide);
    return exitGridArtForDirection(dir, kind);
}

ExitGridArt ExitGridPlacementManager::artForLine(const std::vector<int>& orderedHexes,
    MarkerArt markerArt, bool flipSide) const {
    // An explicit marker-direction override forces one art for every hex (the escape hatch for
    // ambiguous edges); Auto uses the single whole-stroke side. The flip key only affects Auto — an
    // explicit direction is already a fixed side, so there is nothing to flip.
    if (const int dir = explicitDirection(markerArt); dir >= 0) {
        return exitGridArtForDirection(dir, destinationKind());
    }
    return autoArtForLine(orderedHexes, flipSide);
}

std::vector<uint32_t> ExitGridPlacementManager::previewFrmPidsForLine(
    const std::vector<int>& orderedHexes, bool flipSide) const {
    // One whole-stroke art for the whole line: every preview hex gets the same FRM, matching the
    // commit (a clean single-side edge).
    const uint32_t frmPid = artForLine(orderedHexes, _currentMarkerArt, flipSide).frmPid;
    return std::vector<uint32_t>(orderedHexes.size(), frmPid);
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
    const auto* selectionManager = _context.getSelectionManager();
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

            const auto& mapObject = selectedObject->getMapObject();

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
    for (const auto& exitGridObject : exitGrids) {
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

    // Destination-only bulk edit: update only the exit_* fields on every marker and keep each one's
    // existing directional art (pro_pid/frm_pid) — these markers were placed along a line, each with
    // the art for its own segment, so we must not collapse them all to one fixed direction.
    std::vector<ExitGridContext::ExitGridState> afterStates;
    for (const auto& mapObjectPtr : mapObjects) {
        mapObjectPtr->exit_map = newProperties.exitMap;
        mapObjectPtr->exit_position = newProperties.exitPosition;
        mapObjectPtr->exit_elevation = newProperties.exitElevation;
        mapObjectPtr->exit_orientation = newProperties.exitOrientation;

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

    spdlog::info("Updated destination of {} exit grids: map={}", mapObjects.size(), newProperties.exitMap);

    _context.refreshObjects();
    return true;
}

std::size_t ExitGridPlacementManager::createExitGridsForHexes(const std::vector<int>& hexPositions,
    bool flipSide) {
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

    // One whole-stroke art (one consistent side, optionally flipped) shared by every hex, so the bars
    // form a clean continuous edge.
    const ExitGridArt art = artForLine(hexPositions, newProperties.markerArt, flipSide);
    int currentElevation = _context.getCurrentElevation();
    std::vector<std::shared_ptr<MapObject>> createdExitGrids;
    for (const int hexPosition : hexPositions) {
        createdExitGrids.push_back(createExitGridObject(hexPosition, art.proPid, art.frmPid, newProperties));
    }

    if (createdExitGrids.empty()) {
        return 0;
    }

    // Registers the undo action and adds to map; redo runs immediately
    _context.registerExitGridCreation(createdExitGrids, currentElevation);

    spdlog::info("Created {} exit grids along the edge (elevation {})",
        createdExitGrids.size(), currentElevation);
    _showStatus(QString("Created %1 exit grids along the edge").arg(createdExitGrids.size()));
    return createdExitGrids.size();
}

std::vector<int> ExitGridPlacementManager::collectHexesAlongLine(
    const std::vector<sf::Vector2f>& worldVertices) const {
    const auto* hexGrid = _context.getHexagonGrid();
    const auto* viewport = _context.getViewportController();
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
    for (const auto& object : _context.getObjects()) {
        auto mapObjectPtr = object ? object->getMapObjectPtr() : nullptr;
        if (!mapObjectPtr || !mapObjectPtr->isExitGridMarker()) {
            continue;
        }
        if (wanted.contains(static_cast<int>(mapObjectPtr->position))) {
            result.push_back(object);
        }
    }
    return result;
}

std::vector<int> ExitGridPlacementManager::freshHexesForLine(const std::vector<int>& lineHexes,
    const std::set<int>& occupied) {
    // The line hexes that don't already have an exit grid -- the ones a stroke should CREATE on.
    // Empty when every hex is already occupied (the caller bulk-edits the existing edge) or the line
    // is empty. Pure + static so the partial-overlap placement decision is unit-testable without a
    // dialog/context.
    std::vector<int> fresh;
    fresh.reserve(lineHexes.size());
    for (const int hex : lineHexes) {
        if (!occupied.contains(hex)) {
            fresh.push_back(hex);
        }
    }
    return fresh;
}

void ExitGridPlacementManager::selectExitGridsAlongLine(const std::vector<sf::Vector2f>& worldVertices,
    bool flipSide) {
    if (!_markExitsMode || worldVertices.size() < 2) {
        return;
    }

    auto* selectionManager = _context.getSelectionManager();
    if (selectionManager) {
        selectionManager->clearSelection();
    }
    _context.clearSelection();

    const std::vector<int> lineHexes = collectHexesAlongLine(worldVertices);
    const auto existing = collectExitGridsOnHexes(lineHexes);

    // The hexes already occupied by an exit grid.
    std::set<int> occupied;
    for (const auto& object : existing) {
        if (const auto mapObject = object ? object->getMapObjectPtr() : nullptr) {
            occupied.insert(static_cast<int>(mapObject->position));
        }
    }

    // Only treat the stroke as an EDIT when EVERY hex already has an exit grid (the user is re-editing
    // an existing edge): bulk-edit their destination, keeping each one's directional art. A stroke that
    // merely grazes a neighbouring grid must still place -- otherwise a single overlapping hex silently
    // swallows the whole placement (the intermittent "press Enter + OK but nothing appears" bug).
    const std::vector<int> freshHexes = freshHexesForLine(lineHexes, occupied);
    if (freshHexes.empty() && !lineHexes.empty()) {
        bulkEditExistingExitGrids(existing);
        return;
    }

    // Create on the hexes that don't already have a grid, all sharing the single whole-stroke side.
    createExitGridsForHexes(freshHexes, flipSide);
}

} // namespace geck
