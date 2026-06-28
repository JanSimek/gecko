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

    // A single hex has no stroke: the art is the center-facing classification (or the explicit
    // override). No flip on a click-placed lone marker.
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

    // Destination-only edit: update the exit_* fields, keep the directional art (pro_pid/frm_pid).
    // Overwriting the art would collapse every edited marker to one fixed direction.
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

    // Direction-specific art, chosen per hex by the caller — independent of the destination (exit_*).
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

    // The explicit MarkerArt values map 1:1 onto ExitGrid::Direction (0..7); Auto has none, so it
    // returns -1 ("no explicit direction").
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

    // A lone hex has no axis: classify by its outward facing.
    if (orderedHexes.size() < 2) {
        const auto [hx, hy] = screenOf(orderedHexes.front());
        const ExitGridArt art = exitGridArtForFacing(hx, hy, centerX, centerY, kind);
        // Honour a flip even for a lone hex so preview/commit stay consistent.
        if (flipSide) {
            return exitGridArtForDirection(
                flipExitGridDirection(static_cast<int>(art.proPid - ExitGrid::FIRST_EXIT_GRID_PID)),
                kind);
        }
        return art;
    }

    // Whole-stroke classification: first->last delta picks the axis, the midpoint's outward facing picks
    // the side, ONCE — so every hex shares one side (a clean continuous bar) instead of flipping mid-run.
    const auto [fx, fy] = screenOf(orderedHexes.front());
    const auto [lx, ly] = screenOf(orderedHexes.back());
    const auto [mx, my] = screenOf(orderedHexes[orderedHexes.size() / 2]);
    const int dir = exitGridDirectionForLine(lx - fx, ly - fy, mx - centerX, my - centerY, flipSide);
    return exitGridArtForDirection(dir, kind);
}

ExitGridArt ExitGridPlacementManager::artForLine(const std::vector<int>& orderedHexes,
    MarkerArt markerArt, bool flipSide) const {
    // An explicit override forces one art for every hex; Auto uses the whole-stroke side. The flip only
    // affects Auto (an explicit direction is already a fixed side).
    if (const int dir = explicitDirection(markerArt); dir >= 0) {
        return exitGridArtForDirection(dir, destinationKind());
    }
    return autoArtForLine(orderedHexes, flipSide);
}

std::optional<ExitGridSegmentRun> ExitGridPlacementManager::buildSegmentRun(
    sf::Vector2f from, sf::Vector2f to) const {
    const auto* hexGrid = _context.getHexagonGrid();
    const auto* viewport = _context.getViewportController();
    if (!hexGrid || !viewport) {
        return std::nullopt;
    }
    const auto hexOf = [viewport](const sf::Vector2f& v) -> int {
        const auto opt = viewport->worldPosToHexPosition(WorldCoords(v.x, v.y));
        return (opt.has_value() && opt->isValid()) ? opt->value() : -1;
    };
    const int aHex = hexOf(from);
    const int bHex = hexOf(to);
    if (aHex < 0 || bHex < 0) {
        return std::nullopt;
    }
    std::vector<int> segHexes = hexline::hexLine(*hexGrid, aHex, bHex);
    if (segHexes.empty()) {
        return std::nullopt;
    }
    const auto screenOf = [hexGrid](int hexIndex) -> std::pair<int, int> {
        const auto h = hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex));
        return h.has_value() ? std::pair<int, int>{ h->get().x(), h->get().y() }
                             : std::pair<int, int>{ 0, 0 };
    };
    const auto [centerX, centerY] = hexGridCenterScreen(*hexGrid);
    const auto [fx, fy] = screenOf(segHexes.front());
    const auto [lx, ly] = screenOf(segHexes.back());
    const auto [mx, my] = screenOf(segHexes[segHexes.size() / 2]);
    ExitGridSegmentRun run;
    run.hexes = std::move(segHexes);
    run.screenDx = lx - fx;
    run.screenDy = ly - fy;
    run.outwardX = mx - centerX;
    run.outwardY = my - centerY;
    return run;
}

ExitGridArt ExitGridPlacementManager::segmentArt(const ExitGridSegmentRun& run, bool flipSide) const {
    const ExitGridDestinationKind kind = destinationKind();
    // An explicit override forces ONE direction; Auto classifies from the segment's own screen axis +
    // outward side (optionally flipped).
    if (const int forced = explicitDirection(_currentMarkerArt); forced >= 0) {
        return exitGridArtForDirection(forced, kind);
    }
    const int dir = exitGridDirectionForLine(run.screenDx, run.screenDy,
        run.outwardX, run.outwardY, flipSide);
    return exitGridArtForDirection(dir, kind);
}

ExitGridPlacementManager::CommittedSegment ExitGridPlacementManager::classifySegment(
    sf::Vector2f from, sf::Vector2f to, bool flipSide) const {
    CommittedSegment seg;
    const std::optional<ExitGridSegmentRun> run = buildSegmentRun(from, to);
    if (!run.has_value()) {
        return seg; // degenerate/off-grid: nothing to freeze
    }
    // One uniform art for the whole segment, frozen at the given flip. A diagonal segment places ONE
    // object per drawn hex, exactly like a cardinal segment, so the diagonal run stays continuous with
    // any adjoining horizontal/vertical run at the bend. The doubled-band LOOK is display-only: the
    // renderer draws a decorative second texture for diagonals (see RenderingEngine), no extra objects.
    const ExitGridArt art = segmentArt(*run, flipSide);
    seg.hexes = run->hexes;
    seg.art.assign(seg.hexes.size(), art);
    return seg;
}

void ExitGridPlacementManager::flattenSegments(const std::vector<CommittedSegment>& committed,
    const CommittedSegment& live, std::vector<int>& outHexes, std::vector<ExitGridArt>& outArt) {
    outHexes.clear();
    outArt.clear();
    std::set<int> seen;
    // Committed segments first (in commit order), then the live one; first-seen wins, so committed hexes
    // never change as the live segment moves.
    const auto append = [&](const CommittedSegment& seg) {
        for (std::size_t i = 0; i < seg.hexes.size(); ++i) {
            if (seen.insert(seg.hexes[i]).second) {
                outHexes.push_back(seg.hexes[i]);
                outArt.push_back(seg.art[i]);
            }
        }
    };
    for (const CommittedSegment& seg : committed) {
        append(seg);
    }
    append(live);
}

void ExitGridPlacementManager::beginLine() {
    _committedSegments.clear();
}

void ExitGridPlacementManager::commitSegment(sf::Vector2f from, sf::Vector2f to, bool flipSide) {
    // Freeze the just-closed segment at this click's flip. Drop a degenerate/off-grid capture so it
    // leaves no empty placeholder.
    CommittedSegment seg = classifySegment(from, to, flipSide);
    if (!seg.hexes.empty()) {
        _committedSegments.push_back(std::move(seg));
    }
}

void ExitGridPlacementManager::resetLine() {
    _committedSegments.clear();
}

ExitGridPlacementManager::LinePreview ExitGridPlacementManager::previewForLine(
    sf::Vector2f liveFrom, sf::Vector2f liveTo, bool hasLive, bool flipSide) const {
    // The FROZEN committed segments plus the ONE live segment, classified now at the current flip.
    // Committed art stays frozen; only the live segment changes as the cursor moves or Space flips.
    LinePreview preview;
    const CommittedSegment live = hasLive ? classifySegment(liveFrom, liveTo, flipSide)
                                          : CommittedSegment{};
    std::vector<ExitGridArt> art;
    flattenSegments(_committedSegments, live, preview.hexes, art);
    preview.frmPids.reserve(art.size());
    for (const ExitGridArt& a : art) {
        preview.frmPids.push_back(a.frmPid);
    }
    return preview;
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

    // Destination-only bulk edit: update the exit_* fields, keep each marker's directional art — they
    // were placed along a line with per-segment art, so we must not collapse them to one direction.
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

std::size_t ExitGridPlacementManager::createExitGridsForLine(const std::vector<int>& hexes,
    const std::vector<ExitGridArt>& art, const std::set<int>& freshHexes) {
    if (freshHexes.empty()) {
        _showStatus("No hexes found along the edge line");
        return 0;
    }

    ExitGridPropertiesDialog::ExitGridProperties newProperties;
    if (!showPropertiesDialog(newProperties)) {
        return 0; // User cancelled
    }
    rememberDestinationKind(newProperties.exitMap);
    _currentMarkerArt = newProperties.markerArt;

    // `hexes`/`art` are the frozen per-segment classification (parallel), matching the preview. Create
    // only on the FRESH hexes, each with its segment's frozen art.
    int currentElevation = _context.getCurrentElevation();
    std::vector<std::shared_ptr<MapObject>> createdExitGrids;
    for (std::size_t i = 0; i < hexes.size(); ++i) {
        if (!freshHexes.contains(hexes[i])) {
            continue; // already occupied: don't double-place
        }
        createdExitGrids.push_back(
            createExitGridObject(hexes[i], art[i].proPid, art[i].frmPid, newProperties));
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
    std::vector<int> fresh;
    fresh.reserve(lineHexes.size());
    for (const int hex : lineHexes) {
        if (!occupied.contains(hex)) {
            fresh.push_back(hex);
        }
    }
    return fresh;
}

void ExitGridPlacementManager::selectExitGridsAlongLine() {
    if (!_markExitsMode || _committedSegments.empty()) {
        return;
    }

    auto* selectionManager = _context.getSelectionManager();
    if (selectionManager) {
        selectionManager->clearSelection();
    }
    _context.clearSelection();

    // The placement is exactly the FROZEN committed segments (no live segment exists at finalize), so
    // the placed edge is pixel-identical to the last preview.
    std::vector<int> lineHexes;
    std::vector<ExitGridArt> art;
    flattenSegments(_committedSegments, CommittedSegment{}, lineHexes, art);

    const auto existing = collectExitGridsOnHexes(lineHexes);

    // The hexes already occupied by an exit grid.
    std::set<int> occupied;
    for (const auto& object : existing) {
        if (const auto mapObject = object ? object->getMapObjectPtr() : nullptr) {
            occupied.insert(static_cast<int>(mapObject->position));
        }
    }

    // Treat the stroke as an EDIT only when EVERY hex already has a grid: bulk-edit the destination,
    // keeping each one's art. A stroke that merely grazes a neighbour must still place — otherwise a
    // single overlapping hex silently swallows the placement (the "Enter + OK, nothing appears" bug).
    const std::vector<int> freshList = freshHexesForLine(lineHexes, occupied);
    if (freshList.empty() && !lineHexes.empty()) {
        bulkEditExistingExitGrids(existing);
        return;
    }

    // Create on the hexes that don't already have a grid, each with its own frozen per-segment art.
    const std::set<int> freshHexes(freshList.begin(), freshList.end());
    createExitGridsForLine(lineHexes, art, freshHexes);
}

} // namespace geck
