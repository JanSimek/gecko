#include "TilePlacementManager.h"
#include "../core/EditorWidget.h"
#include "../viewport/ViewportController.h"
#include "../../format/map/Map.h"
#include "../../selection/SelectionManager.h"
#include "../../editor/HexagonGrid.h"
#include "../../util/Constants.h"
#include "../../util/TileUtils.h"
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
        spdlog::debug("TilePlacementManager::placeTileAtPosition: No valid position found");
        return;
    }
    
    // Convert hex index to tile index using utility function
    int tileIndex_pos = hexIndexToTileIndex(hexIndex);
    
    // Validate tile bounds
    if (tileIndex_pos >= TILES_PER_ELEVATION) {
        spdlog::debug("TilePlacementManager::placeTileAtPosition: Tile index {} out of bounds", tileIndex_pos);
        return;
    }

    auto& elevationTiles = _editor->ensureElevationTiles(_editor->getCurrentElevation());

    if (tileIndex_pos >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("TilePlacementManager::placeTileAtPosition: Tile index {} out of bounds", 
            tileIndex_pos);
        return;
    }

    // Place the tile
    uint16_t before = isRoof ? elevationTiles[tileIndex_pos].getRoof() : elevationTiles[tileIndex_pos].getFloor();
    if (isRoof) {
        elevationTiles[tileIndex_pos].setRoof(tileIndex);
    } else {
        elevationTiles[tileIndex_pos].setFloor(tileIndex);
    }
    uint16_t after = isRoof ? elevationTiles[tileIndex_pos].getRoof() : elevationTiles[tileIndex_pos].getFloor();

    // Efficiently update just this tile's sprite using hex index for positioning
    _editor->updateTileSprite(hexIndex, isRoof);
    
    _editor->registerTileEdit("Place Tile", { { _editor->getCurrentElevation(), tileIndex_pos, isRoof, before, after } });
}

void TilePlacementManager::fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof) {
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

    auto& elevationTiles = _editor->ensureElevationTiles(_editor->getCurrentElevation());

    std::vector<EditorWidget::TileChange> changes;
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
            changes.push_back({ _editor->getCurrentElevation(), tileIdx, isRoof, before, after });

            // Convert tile index to hex index for sprite update
            int hexIndex = tileIndexToHexIndex(tileIdx);
            
            // Efficiently update this tile's sprite using hex index
            _editor->updateTileSprite(hexIndex, isRoof);
        }
    }

    if (!changes.empty()) {
        _editor->registerTileEdit("Fill Tiles", changes);
    }
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

    auto& elevationTiles = _editor->ensureElevationTiles(_editor->getCurrentElevation());

    std::vector<EditorWidget::TileChange> changes;
    changes.reserve(floorTileIndices.size() + roofTileIndices.size());

    // Replace floor tiles
    for (int tileIdx : floorTileIndices) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            uint16_t before = elevationTiles[tileIdx].getFloor();
            elevationTiles[tileIdx].setFloor(newTileIndex);
            uint16_t after = elevationTiles[tileIdx].getFloor();
            changes.push_back({ _editor->getCurrentElevation(), tileIdx, false, before, after });
            
            // Convert tile index to hex index for sprite update
            int hexIndex = tileIndexToHexIndex(tileIdx);
            
            _editor->updateTileSprite(hexIndex, false); // false = floor tile
        }
    }

    // Replace roof tiles
    for (int tileIdx : roofTileIndices) {
        if (tileIdx >= 0 && tileIdx < static_cast<int>(elevationTiles.size())) {
            uint16_t before = elevationTiles[tileIdx].getRoof();
            elevationTiles[tileIdx].setRoof(newTileIndex);
            uint16_t after = elevationTiles[tileIdx].getRoof();
            changes.push_back({ _editor->getCurrentElevation(), tileIdx, true, before, after });
            
            // Convert tile index to hex index for sprite update
            int hexIndex = tileIndexToHexIndex(tileIdx);
            
            _editor->updateTileSprite(hexIndex, true); // true = roof tile
        }
    }

    if (!changes.empty()) {
        _editor->registerTileEdit("Replace Tiles", changes);
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

void TilePlacementManager::handleTileAreaFill(sf::Vector2f startPos, sf::Vector2f endPos, bool /*isRoof*/) {
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

} // namespace geck
