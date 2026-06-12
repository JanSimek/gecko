#include "pattern/PatternStamper.h"

#include <optional>

#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/frm/Frm.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "pattern/PatternSprite.h"
#include "resource/GameResources.h"
#include "ui/core/TileChange.h"
#include "ui/editing/ObjectCommandController.h"

namespace geck::pattern {

PatternStamper::Plan PatternStamper::plan(const PatternVariant& variant, int targetHex) {
    using namespace geck::hexgrid;
    Plan p;

    const int anchorCol = columnOf(variant.anchorHex);
    const int anchorRow = rowOf(variant.anchorHex);

    // Objects place at hex precision but tiles only at tile precision (hex/2 in both
    // axes). For the hex->tile division to round the same way at capture and stamp time,
    // the target must share the anchor's parity in BOTH column and row; otherwise the
    // tiles round up to half a tile off the objects (visible as the roof sitting off the
    // walls, slightly and differently for each click). Snap both axes to the anchor.
    int targetCol = columnOf(targetHex);
    int targetRow = rowOf(targetHex);
    if (((targetCol ^ anchorCol) & 1) != 0) {
        targetCol += (targetCol > 0) ? -1 : 1;
    }
    if (((targetRow ^ anchorRow) & 1) != 0) {
        targetRow += (targetRow > 0) ? -1 : 1;
    }
    const int snappedTarget = targetRow * WIDTH + targetCol;

    for (const PatternObject& obj : variant.objects) {
        const int col = anchorCol + obj.dxHex;
        const int row = anchorRow + obj.dyHex;
        std::optional<int> placed;
        if (col >= 0 && col < WIDTH && row >= 0 && row < HEIGHT) {
            placed = translate(row * WIDTH + col, variant.anchorHex, snappedTarget);
        }
        if (placed.has_value()) {
            p.objects.push_back({ *placed, obj.proPid, obj.frmPid, obj.direction, obj.flags });
        } else {
            ++p.objectsDropped;
        }
    }

    // Tile offsets are relative to the anchor's tile, so a stamp re-anchors them on the
    // (snapped) target hex's tile. The tile grid is a plain square grid (no parity offset).
    const int targetTileCol = targetCol / 2;
    const int targetTileRow = targetRow / 2;
    const auto resolveTiles = [&](const std::vector<PatternTile>& src, bool isRoof) {
        for (const PatternTile& t : src) {
            const int col = targetTileCol + t.dxTile;
            const int row = targetTileRow + t.dyTile;
            if (col >= 0 && col < HexagonGrid::TILE_GRID_WIDTH && row >= 0 && row < HexagonGrid::TILE_GRID_HEIGHT) {
                p.tiles.push_back({ row * HexagonGrid::TILE_GRID_WIDTH + col, isRoof, t.tileId });
            } else {
                ++p.tilesDropped;
            }
        }
    };
    resolveTiles(variant.floor, false);
    resolveTiles(variant.roof, true);

    return p;
}

PatternStamper::PatternStamper(resource::GameResources& resources, const HexagonGrid& hexgrid,
    ObjectCommandController& controller, Map& map)
    : _resources(resources)
    , _hexgrid(hexgrid)
    , _controller(controller)
    , _map(map) { }

std::shared_ptr<Object> PatternStamper::buildObject(const std::shared_ptr<MapObject>& mapObject, uint32_t frmPid) const {
    auto object = buildSpriteObject(_resources, _hexgrid, frmPid, mapObject->position, mapObject->direction);
    if (object) {
        object->setMapObject(mapObject);
    }
    return object;
}

PatternStamper::Result PatternStamper::stamp(const PatternVariant& variant, int targetHex, int elevation) {
    const Plan p = plan(variant, targetHex);
    Result r;
    r.dropped = p.objectsDropped + p.tilesDropped;
    if (p.objects.empty() && p.tiles.empty()) {
        return r;
    }

    const std::string desc = variant.label.empty() ? "Stamp pattern" : ("Stamp pattern: " + variant.label);
    ScopedUndoBatch batch(_controller, desc);

    for (const ObjectPlacement& op : p.objects) {
        auto mapObject = std::make_shared<MapObject>();
        mapObject->position = op.hex;
        mapObject->elevation = static_cast<uint32_t>(elevation);
        mapObject->direction = op.direction;
        mapObject->frame_number = 0;
        if (auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(op.hex)); hex.has_value()) {
            mapObject->x = static_cast<uint32_t>(hex->get().x());
            mapObject->y = static_cast<uint32_t>(hex->get().y());
        }
        mapObject->pro_pid = op.proPid;
        mapObject->frm_pid = op.frmPid;
        mapObject->flags = op.flags;
        mapObject->critter_index = -1;
        mapObject->map_scripts_pid = -1;
        mapObject->script_id = -1;
        mapObject->amount = 1;

        auto object = buildObject(mapObject, op.frmPid);
        if (object && _controller.registerObjectPlacement(mapObject, object)) {
            ++r.objectsPlaced;
        } else {
            ++r.objectsFailed;
        }
    }

    if (!p.tiles.empty()) {
        std::vector<TileChange> changes;
        changes.reserve(p.tiles.size());
        const auto& tilesByElevation = _map.getMapFile().tiles;
        const auto it = tilesByElevation.find(elevation);
        for (const TilePlacement& tp : p.tiles) {
            uint16_t before = static_cast<uint16_t>(Map::EMPTY_TILE);
            if (it != tilesByElevation.end() && tp.tileIndex >= 0
                && tp.tileIndex < static_cast<int>(it->second.size())) {
                before = tp.isRoof ? it->second[tp.tileIndex].getRoof() : it->second[tp.tileIndex].getFloor();
            }
            changes.push_back({ elevation, tp.tileIndex, tp.isRoof, before, tp.tileId });
        }
        _controller.applyTileChanges(changes, true);
        _controller.registerTileEdit(desc + " (tiles)", changes);
        r.tilesPainted = static_cast<int>(changes.size());
    }

    r.success = true;
    return r;
}

} // namespace geck::pattern
