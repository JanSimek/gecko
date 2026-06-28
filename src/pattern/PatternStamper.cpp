#include "pattern/PatternStamper.h"

#include <optional>

#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "pattern/FillPlan.h"
#include "pattern/PatternSprite.h"
#include "pattern/PlacementBatch.h"
#include "resource/GameResources.h"
#include "editing/commands/ObjectCommandController.h"

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
    ObjectCommandController& controller, Map& map, bool buildSprites)
    : _resources(resources)
    , _hexgrid(hexgrid)
    , _controller(controller)
    , _map(map)
    , _buildSprites(buildSprites) { }

std::shared_ptr<Object> PatternStamper::buildObject(const std::shared_ptr<MapObject>& mapObject, uint32_t frmPid) const {
    auto object = buildSpriteObject(_resources, _hexgrid, frmPid, mapObject->position, mapObject->direction);
    if (object) {
        object->setMapObject(mapObject);
    }
    return object;
}

std::shared_ptr<MapObject> PatternStamper::makeMapObject(const ObjectPlacement& placement, int elevation) const {
    auto mapObject = std::make_shared<MapObject>();
    mapObject->position = placement.hex;
    mapObject->elevation = static_cast<uint32_t>(elevation);
    mapObject->direction = placement.direction;
    mapObject->frame_number = 0;
    if (const auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(placement.hex)); hex.has_value()) {
        mapObject->x = static_cast<uint32_t>(hex->get().x());
        mapObject->y = static_cast<uint32_t>(hex->get().y());
    }
    mapObject->pro_pid = placement.proPid;
    mapObject->frm_pid = placement.frmPid;
    mapObject->flags = placement.flags;
    mapObject->critter_index = -1;
    mapObject->map_scripts_pid = -1;
    mapObject->script_id = -1;
    mapObject->amount = 1;
    return mapObject;
}

void PatternStamper::appendTiles(FillPlan& out, const std::vector<TilePlacement>& tiles, int elevation) const {
    if (tiles.empty()) {
        return;
    }
    const auto& tilesByElevation = _map.getMapFile().tiles;
    const auto it = tilesByElevation.find(elevation);
    out.tiles.reserve(out.tiles.size() + tiles.size());
    for (const TilePlacement& tp : tiles) {
        uint16_t before = static_cast<uint16_t>(Map::EMPTY_TILE);
        if (it != tilesByElevation.end() && tp.tileIndex >= 0 && tp.tileIndex < static_cast<int>(it->second.size())) {
            before = tp.isRoof ? it->second[tp.tileIndex].getRoof() : it->second[tp.tileIndex].getFloor();
        }
        out.tiles.push_back({ elevation, tp.tileIndex, tp.isRoof, before, tp.tileId });
    }
}

void PatternStamper::planInto(FillPlan& out, const PatternVariant& variant, int targetHex, int elevation) const {
    const Plan p = plan(variant, targetHex);
    out.dropped += p.objectsDropped + p.tilesDropped;

    out.objects.reserve(out.objects.size() + p.objects.size());
    for (const ObjectPlacement& op : p.objects) {
        auto mapObject = makeMapObject(op, elevation);
        // GUI: build the visual so the object draws (needs resolvable art; a null one is left in
        // the entry and PlacementBatch counts it failed). Headless: no sprite — replay records the
        // MapObject as data, so a stamp's objects land even with no art or GL.
        std::shared_ptr<Object> object = _buildSprites ? buildObject(mapObject, op.frmPid) : nullptr;
        out.objects.push_back({ std::move(mapObject), std::move(object) });
    }

    appendTiles(out, p.tiles, elevation);
}

PatternStamper::Result PatternStamper::stamp(const PatternVariant& variant, int targetHex, int elevation) {
    FillPlan fp;
    planInto(fp, variant, targetHex, elevation);

    Result r;
    r.dropped = fp.dropped;
    if (fp.objects.empty() && fp.tiles.empty()) {
        return r;
    }

    const std::string desc = variant.label.empty() ? "Stamp pattern" : ("Stamp pattern: " + variant.label);
    const PlacementBatch::Result br = PlacementBatch::replay(_controller, fp, _buildSprites, desc);
    r.objectsPlaced = br.objectsPlaced;
    r.objectsFailed = br.objectsFailed;
    r.tilesPainted = br.tilesPainted;
    r.success = true;
    return r;
}

} // namespace geck::pattern
