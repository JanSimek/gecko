#include "pattern/PatternBuilder.h"

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "selection/SelectionState.h"
#include "util/TileUtils.h"

namespace geck::pattern {

PatternVariant PatternBuilder::buildVariant(
    const std::vector<const MapObject*>& objects,
    const std::vector<TileSelection>& floorTiles,
    const std::vector<TileSelection>& roofTiles,
    int anchorHex,
    const std::string& label) {
    using namespace geck::hexgrid;

    PatternVariant variant;
    variant.label = label;
    variant.anchorHex = anchorHex;

    const int anchorCol = columnOf(anchorHex);
    const int anchorRow = rowOf(anchorHex);
    for (const MapObject* obj : objects) {
        if (obj == nullptr) {
            continue;
        }
        variant.objects.push_back(PatternObject{
            columnOf(obj->position) - anchorCol,
            rowOf(obj->position) - anchorRow,
            obj->pro_pid,
            obj->frm_pid,
            obj->direction,
            obj->flags });
    }

    // Tile offsets are relative to the anchor's tile (the tile containing the anchor hex).
    const int anchorTileCol = anchorCol / 2;
    const int anchorTileRow = anchorRow / 2;
    const auto addTiles = [&](const std::vector<TileSelection>& src, std::vector<PatternTile>& dst) {
        for (const auto& [tileIndex, tileId] : src) {
            const int col = tileIndex % HexagonGrid::TILE_GRID_WIDTH;
            const int row = tileIndex / HexagonGrid::TILE_GRID_WIDTH;
            dst.push_back(PatternTile{ col - anchorTileCol, row - anchorTileRow, tileId });
        }
    };
    addTiles(floorTiles, variant.floor);
    addTiles(roofTiles, variant.roof);

    return variant;
}

std::optional<Pattern> PatternBuilder::fromSelection(const selection::SelectionState& selection,
    Map& map, int elevation, const std::string& name) {
    std::vector<const MapObject*> objects;
    for (const auto& object : selection.getObjects()) {
        if (object && object->hasMapObject()) {
            objects.push_back(object->getMapObjectPtr().get());
        }
    }

    const auto& tilesByElevation = map.getMapFile().tiles;
    const auto elevationIt = tilesByElevation.find(elevation);
    const auto readTiles = [&](const std::vector<int>& indices, bool isRoof) {
        std::vector<TileSelection> out;
        out.reserve(indices.size());
        for (int index : indices) {
            uint16_t tileId = 0;
            if (elevationIt != tilesByElevation.end() && index >= 0
                && index < static_cast<int>(elevationIt->second.size())) {
                tileId = isRoof ? elevationIt->second[index].getRoof() : elevationIt->second[index].getFloor();
            }
            out.emplace_back(index, tileId);
        }
        return out;
    };
    const std::vector<TileSelection> floorTiles = readTiles(selection.getFloorTileIndices(), false);
    const std::vector<TileSelection> roofTiles = readTiles(selection.getRoofTileIndices(), true);

    if (objects.empty() && floorTiles.empty() && roofTiles.empty()) {
        return std::nullopt;
    }

    int anchorHex = 0;
    if (!objects.empty()) {
        anchorHex = objects.front()->position;
    } else if (!floorTiles.empty()) {
        anchorHex = tileIndexToHexIndex(floorTiles.front().first);
    } else {
        anchorHex = tileIndexToHexIndex(roofTiles.front().first);
    }

    Pattern pattern;
    pattern.name = name;
    pattern.variants.push_back(buildVariant(objects, floorTiles, roofTiles, anchorHex, "default"));
    return pattern;
}

} // namespace geck::pattern
