#include "editing/commands/EdgeEditService.h"

#include <utility>

#include "editing/commands/UndoBatcher.h"
#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "util/UndoStack.h"

namespace geck {

namespace {
    bool validElevation(int elevation) {
        return elevation >= 0 && elevation < MapEdge::ELEVATION_COUNT;
    }

    int clampCoord(int value, int maxInclusive) {
        return value < 0 ? 0 : (value > maxInclusive ? maxInclusive : value);
    }

    void assignRectSide(MapEdge::Rect& rect, EdgeEditService::Side side, int value) {
        switch (side) {
            case EdgeEditService::LEFT:
                rect.left = value;
                break;
            case EdgeEditService::TOP:
                rect.top = value;
                break;
            case EdgeEditService::RIGHT:
                rect.right = value;
                break;
            case EdgeEditService::BOTTOM:
                rect.bottom = value;
                break;
        }
    }
} // namespace

EdgeEditService::EdgeEditService(std::unique_ptr<Map>& map, UndoBatcher& batcher)
    : _map(map)
    , _batcher(batcher) {
}

const std::optional<MapEdge>& EdgeEditService::edge() const {
    static const std::optional<MapEdge> none;
    return _map ? _map->edge() : none;
}

bool EdgeEditService::hasEdge() const {
    return _map && _map->edge().has_value();
}

int EdgeEditService::zoneCount(int elevation) const {
    if (!hasEdge() || !validElevation(elevation)) {
        return 0;
    }
    return static_cast<int>(_map->edge()->elevations[elevation].zones.size());
}

void EdgeEditService::applyEdgeSnapshot(const std::optional<MapEdge>& snapshot) {
    if (_map) {
        _map->setEdge(snapshot);
    }
}

bool EdgeEditService::recordEdgeEdit(const std::string& description, std::optional<MapEdge> before) {
    if (!_map) {
        return false;
    }
    // The caller already applied the edit; capture the resulting "after" state and skip recording a
    // no-op so an unchanged drag/click doesn't clutter the undo stack.
    std::optional<MapEdge> after = _map->edge();
    if (after == before) {
        return false;
    }

    UndoCommand cmd;
    cmd.description = description;
    cmd.undo = [this, before = std::move(before)]() { applyEdgeSnapshot(before); };
    cmd.redo = [this, after = std::move(after)]() { applyEdgeSnapshot(after); };
    return _batcher.push(std::move(cmd));
}

int EdgeEditService::addZone(int elevation, const MapEdge::Rect& seed) {
    if (!_map || !validElevation(elevation)) {
        return -1;
    }
    std::optional<MapEdge> before = _map->edge();
    MapEdge working = _map->edge().value_or(MapEdge{});
    working.elevations[elevation].zones.push_back(seed);
    const int index = static_cast<int>(working.elevations[elevation].zones.size()) - 1;
    _map->setEdge(std::move(working));
    recordEdgeEdit("Add Edge Zone", std::move(before));
    return index;
}

bool EdgeEditService::deleteZone(int elevation, int zoneIndex) {
    if (!hasEdge() || !validElevation(elevation)) {
        return false;
    }
    MapEdge working = *_map->edge();
    auto& zones = working.elevations[elevation].zones;
    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zones.size())) {
        return false;
    }
    std::optional<MapEdge> before = _map->edge();
    zones.erase(zones.begin() + zoneIndex);
    _map->setEdge(std::move(working));
    return recordEdgeEdit("Delete Edge Zone", std::move(before));
}

bool EdgeEditService::setZoneSide(int elevation, int zoneIndex, Side side, int hexIndex) {
    if (!hasEdge() || !validElevation(elevation)) {
        return false;
    }
    if (hexIndex < 0 || hexIndex >= HexagonGrid::POSITION_COUNT) {
        return false;
    }
    MapEdge working = *_map->edge();
    auto& zones = working.elevations[elevation].zones;
    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zones.size())) {
        return false;
    }
    std::optional<MapEdge> before = _map->edge();
    assignRectSide(zones[zoneIndex], side, hexIndex);
    _map->setEdge(std::move(working));
    return recordEdgeEdit("Move Edge Side", std::move(before));
}

std::optional<MapEdge> EdgeEditService::snapshot() const {
    return _map ? _map->edge() : std::nullopt;
}

void EdgeEditService::previewZoneSide(int elevation, int zoneIndex, Side side, int hexIndex) {
    if (!hasEdge() || !validElevation(elevation)) {
        return;
    }
    if (hexIndex < 0 || hexIndex >= HexagonGrid::POSITION_COUNT) {
        return;
    }
    MapEdge working = *_map->edge();
    auto& zones = working.elevations[elevation].zones;
    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zones.size())) {
        return;
    }
    assignRectSide(zones[zoneIndex], side, hexIndex);
    _map->setEdge(std::move(working)); // live preview: no undo recorded
}

void EdgeEditService::restore(const std::optional<MapEdge>& before) {
    applyEdgeSnapshot(before);
}

bool EdgeEditService::commitEdit(const std::string& description, std::optional<MapEdge> before) {
    return recordEdgeEdit(description, std::move(before));
}

bool EdgeEditService::upgradeToVersion2() {
    if (!hasEdge() || _map->edge()->version == 2) {
        return false;
    }
    std::optional<MapEdge> before = _map->edge();
    MapEdge working = *_map->edge();
    working.version = 2;
    _map->setEdge(std::move(working));
    return recordEdgeEdit("Enable Angled Map Edge (v2)", std::move(before));
}

bool EdgeEditService::setSquareSide(int elevation, Side side, int colOrRow) {
    if (!hasEdge() || !validElevation(elevation)) {
        return false;
    }
    std::optional<MapEdge> before = _map->edge();
    MapEdge working = *_map->edge();
    MapEdge::Rect& square = working.elevations[elevation].squareRect;
    switch (side) {
        case LEFT:
            square.left = clampCoord(colOrRow, MapEdge::SQUARE_GRID_WIDTH - 1);
            break;
        case RIGHT:
            square.right = clampCoord(colOrRow, MapEdge::SQUARE_GRID_WIDTH - 1);
            break;
        case TOP:
            square.top = clampCoord(colOrRow, MapEdge::SQUARE_GRID_HEIGHT - 1);
            break;
        case BOTTOM:
            square.bottom = clampCoord(colOrRow, MapEdge::SQUARE_GRID_HEIGHT - 1);
            break;
    }
    _map->setEdge(std::move(working));
    return recordEdgeEdit("Move Square Edge Side", std::move(before));
}

bool EdgeEditService::toggleClipSide(int elevation, Side side) {
    if (!hasEdge() || !validElevation(elevation)) {
        return false;
    }
    std::optional<MapEdge> before = _map->edge();
    MapEdge working = *_map->edge();
    MapEdge::ClipSides& clip = working.elevations[elevation].clipSides;
    switch (side) {
        case LEFT:
            clip.left = !clip.left;
            break;
        case TOP:
            clip.top = !clip.top;
            break;
        case RIGHT:
            clip.right = !clip.right;
            break;
        case BOTTOM:
            clip.bottom = !clip.bottom;
            break;
    }
    _map->setEdge(std::move(working));
    return recordEdgeEdit("Toggle Edge Clip Side", std::move(before));
}

bool EdgeEditService::resetSquare(int elevation) {
    if (!hasEdge() || !validElevation(elevation)) {
        return false;
    }
    std::optional<MapEdge> before = _map->edge();
    MapEdge working = *_map->edge();
    working.elevations[elevation].squareRect
        = MapEdge::Rect{ MapEdge::SQUARE_GRID_WIDTH - 1, 0, 0, MapEdge::SQUARE_GRID_HEIGHT - 1 };
    working.elevations[elevation].clipSides = MapEdge::ClipSides{};
    _map->setEdge(std::move(working));
    return recordEdgeEdit("Reset Square Edge", std::move(before));
}

} // namespace geck
