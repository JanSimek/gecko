#include "TilePlacementManager.h"
#include "../EditorWidget.h"
#include "../../format/map/Map.h"
#include "../../selection/SelectionManager.h"
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
    int hexIndex = worldPosToHexPosition(worldPos);
    if (hexIndex < 0) {
        spdlog::debug("TilePlacementManager::placeTileAtPosition: No tile found at worldPos ({:.1f}, {:.1f})",
            worldPos.x, worldPos.y);
        return;
    }

    auto& mapFile = _editor->getMapFile();
    auto& elevationTiles = mapFile.tiles[_editor->getCurrentElevation()];

    if (hexIndex >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("TilePlacementManager::placeTileAtPosition: Hex index {} out of bounds", hexIndex);
        return;
    }

    // Place the tile
    if (isRoof) {
        elevationTiles[hexIndex].setRoof(tileIndex);
    } else {
        elevationTiles[hexIndex].setFloor(tileIndex);
    }

    // Efficiently update just this tile's sprite
    updateTileSprite(hexIndex, isRoof);

    spdlog::debug("TilePlacementManager::placeTileAtPosition: Placed tile {} at hex {} (roof: {})",
        tileIndex, hexIndex, isRoof);
}

void TilePlacementManager::fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof) {
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
    if (_tilePlacementMode && _tilePlacementIndex >= 0) {
        float left = std::min(startPos.x, endPos.x);
        float top = std::min(startPos.y, endPos.y);
        float width = std::abs(endPos.x - startPos.x);
        float height = std::abs(endPos.y - startPos.y);
        sf::FloatRect fillArea({left, top}, {width, height});
        fillAreaWithTile(_tilePlacementIndex, fillArea, isRoof);
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

int TilePlacementManager::worldPosToHexPosition(sf::Vector2f worldPos) const {
    return _editor->worldPosToHexPosition(worldPos);
}

} // namespace geck