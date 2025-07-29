#include "TilePlacementManager.h"
#include "../core/EditorWidget.h"
#include "../viewport/ViewportController.h"
#include "../../format/map/Map.h"
#include "../../selection/SelectionManager.h"
#include "../../editor/HexagonGrid.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace geck {

TilePlacementManager::TilePlacementManager(EditorWidget* editor)
    : _editor(editor) {
}

void TilePlacementManager::placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof) {
    if (!_editor->getMap()) {
        spdlog::warn("TilePlacementManager::placeTileAtPosition: No map loaded");
        return;
    }

    // Convert world position to hex index
    int hexIndex = _editor->getViewportController()->worldPosToHexIndex(worldPos);
    if (hexIndex < 0) {
        spdlog::debug("TilePlacementManager::placeTileAtPosition: No tile found at worldPos ({:.1f}, {:.1f})",
            worldPos.x, worldPos.y);
        return;
    }

    // Convert hex coordinates (200x200 grid) to tile coordinates (100x100 grid)
    // Hex grid: 0-39999 positions, Tile grid: 0-9999 positions
    int hexX = hexIndex % HexagonGrid::GRID_WIDTH;  // 0-199
    int hexY = hexIndex / HexagonGrid::GRID_WIDTH;  // 0-199
    int tileX = hexX / 2;  // 0-99
    int tileY = hexY / 2;  // 0-99
    int tileIndex_pos = tileX + (tileY * Map::COLS); // 0-9999

    auto& mapFile = _editor->getMapFile();
    auto& elevationTiles = mapFile.tiles[_editor->getCurrentElevation()];

    if (tileIndex_pos >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("TilePlacementManager::placeTileAtPosition: Tile index {} out of bounds (converted from hex {})", 
            tileIndex_pos, hexIndex);
        return;
    }

    // Place the tile
    if (isRoof) {
        elevationTiles[tileIndex_pos].setRoof(tileIndex);
    } else {
        elevationTiles[tileIndex_pos].setFloor(tileIndex);
    }

    // Efficiently update just this tile's sprite
    updateTileSprite(tileIndex_pos, isRoof);

    spdlog::debug("TilePlacementManager::placeTileAtPosition: Placed tile {} at tile {} (converted from hex {}, roof: {})",
        tileIndex, tileIndex_pos, hexIndex, isRoof);
}

void TilePlacementManager::fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof) {
    spdlog::info("TilePlacementManager::fillAreaWithTile called with tile {} area ({:.1f},{:.1f},{:.1f},{:.1f}) roof: {}", 
        tileIndex, area.position.x, area.position.y, area.size.x, area.size.y, isRoof);
    
    if (!_editor->getMap()) {
        spdlog::warn("TilePlacementManager::fillAreaWithTile: No map loaded");
        return;
    }

    // Use the selection system to get tiles in area
    std::vector<int> tilesToFill = _editor->getSelectionManager()->getTilesInArea(area, isRoof, _editor->getCurrentElevation());

    if (tilesToFill.empty()) {
        spdlog::debug("TilePlacementManager::fillAreaWithTile: No tiles found in area");
        return;
    }

    auto& mapFile = _editor->getMapFile();
    auto& elevationTiles = mapFile.tiles[_editor->getCurrentElevation()];

    int tilesPlaced = 0;
    for (int hexIndex : tilesToFill) {
        if (hexIndex >= 0 && hexIndex < static_cast<int>(elevationTiles.size())) {
            if (isRoof) {
                elevationTiles[hexIndex].setRoof(tileIndex);
            } else {
                elevationTiles[hexIndex].setFloor(tileIndex);
            }

            // Efficiently update this tile's sprite
            updateTileSprite(hexIndex, isRoof);
            tilesPlaced++;
        }
    }

    spdlog::info("TilePlacementManager::fillAreaWithTile: Filled {} tiles with tile {} (roof: {})",
        tilesPlaced, tileIndex, isRoof);
}

void TilePlacementManager::replaceSelectedTiles(int newTileIndex) {
    if (!_editor->getMap()) {
        spdlog::warn("TilePlacementManager::replaceSelectedTiles: No map loaded");
        return;
    }

    const auto& selection = _editor->getSelectionManager()->getCurrentSelection();

    // Replace both floor and roof tiles based on what's actually selected
    std::vector<int> floorTileIndices = selection.getFloorTileIndices();
    std::vector<int> roofTileIndices = selection.getRoofTileIndices();

    if (floorTileIndices.empty() && roofTileIndices.empty()) {
        spdlog::debug("TilePlacementManager::replaceSelectedTiles: No tiles selected for replacement");
        return;
    }

    auto& mapFile = _editor->getMapFile();
    auto& elevationTiles = mapFile.tiles[_editor->getCurrentElevation()];

    int tilesReplaced = 0;

    // Replace floor tiles
    for (int hexIndex : floorTileIndices) {
        if (hexIndex >= 0 && hexIndex < static_cast<int>(elevationTiles.size())) {
            elevationTiles[hexIndex].setFloor(newTileIndex);
            updateTileSprite(hexIndex, false); // false = floor tile
            tilesReplaced++;
        }
    }

    // Replace roof tiles
    for (int hexIndex : roofTileIndices) {
        if (hexIndex >= 0 && hexIndex < static_cast<int>(elevationTiles.size())) {
            elevationTiles[hexIndex].setRoof(newTileIndex);
            updateTileSprite(hexIndex, true); // true = roof tile
            tilesReplaced++;
        }
    }

    spdlog::info("TilePlacementManager::replaceSelectedTiles: Replaced {} tiles with tile {} ({} floor, {} roof)",
        tilesReplaced, newTileIndex, floorTileIndices.size(), roofTileIndices.size());
}

void TilePlacementManager::setTilePlacementMode(bool enabled, int tileIndex, bool isRoof) {
    _tilePlacementMode = enabled;
    _tilePlacementIndex = tileIndex;
    _tilePlacementIsRoof = isRoof;

    if (enabled) {
        spdlog::debug("TilePlacementManager: Enabled tile placement mode (tile: {}, roof: {})", tileIndex, isRoof);
    } else {
        spdlog::debug("TilePlacementManager: Disabled tile placement mode");
    }
}

void TilePlacementManager::setTilePlacementAreaFill(bool enabled) {
    _tilePlacementAreaFill = enabled;
    spdlog::debug("TilePlacementManager: Area fill mode {}", enabled ? "enabled" : "disabled");
}

void TilePlacementManager::setTilePlacementReplaceMode(bool enabled) {
    _tilePlacementReplaceMode = enabled;
    spdlog::debug("TilePlacementManager: Replace mode {}", enabled ? "enabled" : "disabled");
}

void TilePlacementManager::handleTilePlacement(sf::Vector2f worldPos, bool isRoof) {
    if (_tilePlacementMode && _tilePlacementIndex >= 0) {
        placeTileAtPosition(_tilePlacementIndex, worldPos, isRoof);
    }
}

void TilePlacementManager::handleTileAreaFill(sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof) {
    spdlog::info("TilePlacementManager::handleTileAreaFill called with start ({:.1f},{:.1f}) end ({:.1f},{:.1f}) roof: {} mode: {} index: {}", 
        startPos.x, startPos.y, endPos.x, endPos.y, isRoof, _tilePlacementMode, _tilePlacementIndex);
    
    if (_tilePlacementMode && _tilePlacementIndex >= 0) {
        float left = std::min(startPos.x, endPos.x);
        float top = std::min(startPos.y, endPos.y);
        float width = std::abs(endPos.x - startPos.x);
        float height = std::abs(endPos.y - startPos.y);
        sf::FloatRect fillArea({left, top}, {width, height});
        fillAreaWithTile(_tilePlacementIndex, fillArea, isRoof);
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
    
    spdlog::debug("TilePlacementManager: Reset all tile placement state");
}

void TilePlacementManager::updateTileSprite(int hexIndex, bool isRoof) {
    _editor->updateTileSprite(hexIndex, isRoof);
}


} // namespace geck