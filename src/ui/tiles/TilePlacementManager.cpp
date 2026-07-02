#include "TilePlacementManager.h"
#include "editor/TileChange.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "selection/SelectionManager.h"
#include "editor/HexagonGrid.h"
#include "util/Constants.h"
#include "util/TileUtils.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace geck {

TilePlacementManager::TilePlacementManager(TilePlacementContext& context)
    : _context(context) {
}

void TilePlacementManager::placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof) {
    if (!_context.getMap()) {
        spdlog::warn("TilePlacementManager::placeTileAtPosition: No map loaded");
        return;
    }

    // Resolve the click to a tile with the SAME nearest-tile-centre projection the hover/
    // selection path uses (EditorWidget::getTileAtPosition -> screenToTileIndex), so the tile
    // that gets painted is exactly the tile under the cursor. The old path snapped to the
    // nearest hex and halved, which picks an adjacent tile away from tile centres — the painted
    // tile then disagreed with the highlighted one. screenToTileIndex applies the roof offset
    // internally, so no manual ROOF_OFFSET adjustment is needed here.
    const auto resolvedTile = screenToTileIndex(worldPos.x, worldPos.y, isRoof);
    if (!resolvedTile.has_value()) {
        spdlog::debug("TilePlacementManager::placeTileAtPosition: No valid position found");
        return;
    }
    int tileIndex_pos = *resolvedTile;

    // updateTileSprite works in hex space; recover the tile's canonical top-left hex.
    // tileIndexToHexIndex -> tileIndexForPosition is a lossless round-trip back to tileIndex_pos.
    int hexIndex = tileIndexToHexIndex(tileIndex_pos);

    auto& elevationTiles = _context.ensureElevationTiles(_context.getCurrentElevation());

    if (tileIndex_pos >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("TilePlacementManager::placeTileAtPosition: Tile index {} out of bounds",
            tileIndex_pos);
        return;
    }

    uint16_t before = isRoof ? elevationTiles[tileIndex_pos].getRoof() : elevationTiles[tileIndex_pos].getFloor();
    if (isRoof) {
        elevationTiles[tileIndex_pos].setRoof(tileIndex);
    } else {
        elevationTiles[tileIndex_pos].setFloor(tileIndex);
    }
    uint16_t after = isRoof ? elevationTiles[tileIndex_pos].getRoof() : elevationTiles[tileIndex_pos].getFloor();

    _context.updateTileSprite(hexIndex, isRoof);

    _context.registerTileEdit("Place Tile", { { _context.getCurrentElevation(), tileIndex_pos, isRoof, before, after } });
}

void TilePlacementManager::fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof) {
    if (!_context.getMap()) {
        spdlog::warn("TilePlacementManager::fillAreaWithTile: No map loaded");
        return;
    }

    std::vector<int> tilesToFill = _context.getSelectionManager()->getTilesInAreaIncludingEmpty(area, isRoof, _context.getCurrentElevation());

    if (tilesToFill.empty()) {
        spdlog::debug("TilePlacementManager::fillAreaWithTile: No tiles found in area");
        return;
    }

    auto& elevationTiles = _context.ensureElevationTiles(_context.getCurrentElevation());

    std::vector<TileChange> changes;
    changes.reserve(tilesToFill.size());

    for (int tileIdx : tilesToFill) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            uint16_t before = isRoof ? elevationTiles[tileIdx].getRoof() : elevationTiles[tileIdx].getFloor();
            if (isRoof) {
                elevationTiles[tileIdx].setRoof(tileIndex);
            } else {
                elevationTiles[tileIdx].setFloor(tileIndex);
            }
            uint16_t after = isRoof ? elevationTiles[tileIdx].getRoof() : elevationTiles[tileIdx].getFloor();
            changes.push_back({ _context.getCurrentElevation(), tileIdx, isRoof, before, after });

            int hexIndex = tileIndexToHexIndex(tileIdx);
            _context.updateTileSprite(hexIndex, isRoof);
        }
    }

    if (!changes.empty()) {
        _context.registerTileEdit("Fill Tiles", changes);
    }
}

void TilePlacementManager::replaceSelectedTiles(int newTileIndex) {
    if (!_context.getMap()) {
        spdlog::warn("TilePlacementManager::replaceSelectedTiles: No map loaded");
        return;
    }

    const auto& selection = _context.getSelectionManager()->getCurrentSelection();

    std::vector<int> floorTileIndices = selection.getFloorTileIndices();
    std::vector<int> roofTileIndices = selection.getRoofTileIndices();

    if (floorTileIndices.empty() && roofTileIndices.empty()) {
        spdlog::debug("TilePlacementManager::replaceSelectedTiles: No tiles selected for replacement");
        return;
    }

    auto& elevationTiles = _context.ensureElevationTiles(_context.getCurrentElevation());

    std::vector<TileChange> changes;
    changes.reserve(floorTileIndices.size() + roofTileIndices.size());

    for (int tileIdx : floorTileIndices) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            uint16_t before = elevationTiles[tileIdx].getFloor();
            elevationTiles[tileIdx].setFloor(newTileIndex);
            uint16_t after = elevationTiles[tileIdx].getFloor();
            changes.push_back({ _context.getCurrentElevation(), tileIdx, false, before, after });

            int hexIndex = tileIndexToHexIndex(tileIdx);
            _context.updateTileSprite(hexIndex, false); // false = floor tile
        }
    }

    for (int tileIdx : roofTileIndices) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            uint16_t before = elevationTiles[tileIdx].getRoof();
            elevationTiles[tileIdx].setRoof(newTileIndex);
            uint16_t after = elevationTiles[tileIdx].getRoof();
            changes.push_back({ _context.getCurrentElevation(), tileIdx, true, before, after });

            int hexIndex = tileIndexToHexIndex(tileIdx);
            _context.updateTileSprite(hexIndex, true); // true = roof tile
        }
    }

    if (!changes.empty()) {
        _context.registerTileEdit("Replace Tiles", changes);
    }
}

void TilePlacementManager::setTilePlacementMode(bool enabled, int tileIndex, bool isRoof) {
    _tilePlacementMode = enabled;
    _tilePlacementIndex = tileIndex;
    _tilePlacementIsRoof = isRoof;
}

void TilePlacementManager::setTilePlacementAreaFill(bool enabled) {
    _tilePlacementAreaFill = enabled;
}

void TilePlacementManager::setTilePlacementReplaceMode(bool enabled) {
    _tilePlacementReplaceMode = enabled;
}

void TilePlacementManager::handleTilePlacement(sf::Vector2f worldPos, bool /*isRoof*/) {
    if (_tilePlacementMode && _tilePlacementIndex >= 0) {
        // If tiles are already selected, replace them instead of placing a new tile
        if (_context.getSelectionManager()->hasSelection()) {
            replaceSelectedTiles(_tilePlacementIndex);
        } else {
            // Use stored roof state instead of the parameter to ensure consistency
            placeTileAtPosition(_tilePlacementIndex, worldPos, _tilePlacementIsRoof);
        }
    }
}

void TilePlacementManager::handleTileAreaFill(sf::Vector2f startPos, sf::Vector2f endPos, bool /*isRoof*/) {
    if (_tilePlacementMode && _tilePlacementIndex >= 0) {
        // If tiles are already selected, replace them instead of area filling
        if (_context.getSelectionManager()->hasSelection()) {
            replaceSelectedTiles(_tilePlacementIndex);
        } else {
            float left = std::min(startPos.x, endPos.x);
            float top = std::min(startPos.y, endPos.y);
            float width = std::abs(endPos.x - startPos.x);
            float height = std::abs(endPos.y - startPos.y);
            sf::FloatRect fillArea({ left, top }, { width, height });
            fillAreaWithTile(_tilePlacementIndex, fillArea, _tilePlacementIsRoof);
        }
    } else {
        spdlog::warn("TilePlacementManager::handleTileAreaFill: Not in tile placement mode or no tile selected");
    }
}

void TilePlacementManager::resetState() {
    _tilePlacementMode = false;
    _tilePlacementAreaFill = false;
    _tilePlacementReplaceMode = false;
    _tilePlacementIndex = -1;
    _tilePlacementIsRoof = false;
}

} // namespace geck
