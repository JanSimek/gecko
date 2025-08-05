#include "TilePlacementManager.h"
#include "../core/EditorWidget.h"
#include "../viewport/ViewportController.h"
#include "../../format/map/Map.h"
#include "../../selection/SelectionManager.h"
#include "../../editor/HexagonGrid.h"
#include "../../util/Constants.h"
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

    // Use the exact same logic as the working selection system
    // Adjust world position for roof offset if placing roof tiles
    sf::Vector2f adjustedWorldPos = worldPos;
    if (isRoof) {
        adjustedWorldPos.y += ROOF_OFFSET;  // Roof tiles are visually offset upward
    }
    
    int hexIndex = _editor->getViewportController()->worldPosToHexIndex(adjustedWorldPos);
    if (hexIndex < 0) {
        spdlog::debug("TilePlacementManager::placeTileAtPosition: No hex found at worldPos ({:.1f}, {:.1f}) adjusted({:.1f}, {:.1f}) [roof: {}]",
            worldPos.x, worldPos.y, adjustedWorldPos.x, adjustedWorldPos.y, isRoof);
        return;
    }
    
    // Convert hex coordinates (200x200 grid) to tile coordinates (100x100 grid)
    int hexX = hexIndex % HexagonGrid::GRID_WIDTH;  // 0-199
    int hexY = hexIndex / HexagonGrid::GRID_WIDTH;  // 0-199
    int tileX = hexX / 2;  // 0-99
    int tileY = hexY / 2;  // 0-99
    int tileIndex_pos = tileY * MAP_WIDTH + tileX; // Convert to tile index
    
    // Validate tile bounds
    if (tileIndex_pos >= TILES_PER_ELEVATION) {
        spdlog::debug("TilePlacementManager::placeTileAtPosition: Tile index {} out of bounds", tileIndex_pos);
        return;
    }

    auto& mapFile = _editor->getMapFile();
    auto& elevationTiles = mapFile.tiles[_editor->getCurrentElevation()];

    if (tileIndex_pos >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("TilePlacementManager::placeTileAtPosition: Tile index {} out of bounds", 
            tileIndex_pos);
        return;
    }

    // Place the tile
    if (isRoof) {
        elevationTiles[tileIndex_pos].setRoof(tileIndex);
    } else {
        elevationTiles[tileIndex_pos].setFloor(tileIndex);
    }

    // Efficiently update just this tile's sprite using hex index for positioning
    updateTileSprite(hexIndex, isRoof);

    spdlog::debug("TilePlacementManager::placeTileAtPosition: Placed tile {} at tile position {} (roof: {})",
        tileIndex, tileIndex_pos, isRoof);
}

void TilePlacementManager::fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof) {
    spdlog::info("TilePlacementManager::fillAreaWithTile called with tile {} area ({:.1f},{:.1f},{:.1f},{:.1f}) roof: {}", 
        tileIndex, area.position.x, area.position.y, area.size.x, area.size.y, isRoof);
    
    if (!_editor->getMap()) {
        spdlog::warn("TilePlacementManager::fillAreaWithTile: No map loaded");
        return;
    }

    // Use the selection system to get tiles in area (including empty tiles for placement)
    std::vector<int> tilesToFill = _editor->getSelectionManager()->getTilesInAreaIncludingEmpty(area, isRoof, _editor->getCurrentElevation());

    if (tilesToFill.empty()) {
        spdlog::debug("TilePlacementManager::fillAreaWithTile: No tiles found in area");
        return;
    }

    auto& mapFile = _editor->getMapFile();
    auto& elevationTiles = mapFile.tiles[_editor->getCurrentElevation()];

    int tilesPlaced = 0;
    for (int tileIdx : tilesToFill) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            if (isRoof) {
                elevationTiles[tileIdx].setRoof(tileIndex);
            } else {
                elevationTiles[tileIdx].setFloor(tileIndex);
            }

            // Convert tile index to hex index for sprite update
            int tileX = tileIdx % MAP_WIDTH;        // 0-99
            int tileY = tileIdx / MAP_WIDTH;        // 0-99
            int hexX = tileX * 2;                   // 0-198
            int hexY = tileY * 2;                   // 0-198  
            int hexIndex = hexY * HexagonGrid::GRID_WIDTH + hexX;
            
            // Efficiently update this tile's sprite using hex index
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
    for (int tileIdx : floorTileIndices) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            elevationTiles[tileIdx].setFloor(newTileIndex);
            
            // Convert tile index to hex index for sprite update
            int tileX = tileIdx % MAP_WIDTH;        // 0-99
            int tileY = tileIdx / MAP_WIDTH;        // 0-99
            int hexX = tileX * 2;                   // 0-198
            int hexY = tileY * 2;                   // 0-198  
            int hexIndex = hexY * HexagonGrid::GRID_WIDTH + hexX;
            
            updateTileSprite(hexIndex, false); // false = floor tile
            tilesReplaced++;
        }
    }

    // Replace roof tiles
    for (int tileIdx : roofTileIndices) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            elevationTiles[tileIdx].setRoof(newTileIndex);
            
            // Convert tile index to hex index for sprite update
            int tileX = tileIdx % MAP_WIDTH;        // 0-99
            int tileY = tileIdx / MAP_WIDTH;        // 0-99
            int hexX = tileX * 2;                   // 0-198
            int hexY = tileY * 2;                   // 0-198  
            int hexIndex = hexY * HexagonGrid::GRID_WIDTH + hexX;
            
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

}

void TilePlacementManager::setTilePlacementAreaFill(bool enabled) {
    _tilePlacementAreaFill = enabled;
}

void TilePlacementManager::setTilePlacementReplaceMode(bool enabled) {
    _tilePlacementReplaceMode = enabled;
}

void TilePlacementManager::handleTilePlacement(sf::Vector2f worldPos, bool isRoof) {
    if (_tilePlacementMode && _tilePlacementIndex >= 0) {
        // Check if there are already selected tiles - if so, replace them instead of placing new tile
        if (_editor->getSelectionManager()->hasSelection()) {
            replaceSelectedTiles(_tilePlacementIndex);
        } else {
            // No selection, place tile at clicked position
            // Use stored roof state instead of parameter to ensure consistency
            placeTileAtPosition(_tilePlacementIndex, worldPos, _tilePlacementIsRoof);
        }
    }
}

void TilePlacementManager::handleTileAreaFill(sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof) {
    if (_tilePlacementMode && _tilePlacementIndex >= 0) {
        // Check if there are already selected tiles - if so, replace them instead of area fill
        if (_editor->getSelectionManager()->hasSelection()) {
            replaceSelectedTiles(_tilePlacementIndex);
        } else {
            // No selection, perform normal area fill
            float left = std::min(startPos.x, endPos.x);
            float top = std::min(startPos.y, endPos.y);
            float width = std::abs(endPos.x - startPos.x);
            float height = std::abs(endPos.y - startPos.y);
            sf::FloatRect fillArea({left, top}, {width, height});
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

void TilePlacementManager::updateTileSprite(int hexIndex, bool isRoof) {
    _editor->updateTileSprite(hexIndex, isRoof);
}


} // namespace geck