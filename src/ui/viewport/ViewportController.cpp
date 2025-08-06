#include "ViewportController.h"
#include "../../editor/Hex.h"
#include "../../util/Constants.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

using namespace geck;

namespace geck {

ViewportController::ViewportController(const HexagonGrid* hexGrid)
    : _hexGrid(hexGrid) {
}

void ViewportController::initialize(sf::Vector2u windowSize) {
    // Initialize with proper aspect ratio management
    updateViewForWindowSize(windowSize);
    
    // Center the view on the map initially
    centerViewOnMap();
    
    spdlog::debug("ViewportController: Initialized with window size {}x{}", 
                  windowSize.x, windowSize.y);
}

void ViewportController::centerViewOnMap() {
    // Calculate map center based on the same tile positioning algorithm used in EditorWidget::loadTileSprites()
    constexpr int centerTileX = MAP_WIDTH / 2;
    constexpr int centerTileY = MAP_HEIGHT / 2;

    float centerX = (MAP_WIDTH - centerTileY - 1) * TILE_X_OFFSET + TILE_Y_OFFSET_LARGE * (centerTileX - 1);
    float centerY = centerTileX * TILE_Y_OFFSET_SMALL + (centerTileY - 1) * TILE_Y_OFFSET_TINY + 1;

    _view.setCenter(sf::Vector2f(centerX, centerY));
    spdlog::debug("ViewportController: Centered view on ({:.1f}, {:.1f})", centerX, centerY);
}

void ViewportController::zoomView(float direction) {
    float newZoom = _zoomLevel;
    
    if (direction > 0) {
        // Zoom in
        newZoom *= (1.0f + ZOOM_STEP);
    } else if (direction < 0) {
        // Zoom out
        newZoom *= (1.0f - ZOOM_STEP);
    }
    
    setZoomLevel(newZoom);
}

void ViewportController::setZoomLevel(float zoom) {
    // Clamp zoom level to valid range
    _zoomLevel = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    
    // Apply zoom to view
    _view.zoom(_zoomLevel / _view.getSize().x * 800.0f);
}

int ViewportController::updateHoverHex(sf::Vector2f worldPos) {
    return worldPosToHexIndex(worldPos);
}

int ViewportController::worldPosToHexIndex(sf::Vector2f worldPos) const {
    if (!_hexGrid) {
        return -1;
    }
    
    // Use improved hex grid position lookup with better accuracy
    uint32_t hexPosition = _hexGrid->positionAt(static_cast<uint32_t>(worldPos.x), static_cast<uint32_t>(worldPos.y));
    
    if (hexPosition == Hex::HEX_OUT_OF_MAP) {
        spdlog::debug("ViewportController::worldPosToHexIndex: world({:.1f}, {:.1f}) -> out of map",
                      worldPos.x, worldPos.y);
        return -1;
    }
    
    // Find the hex index from position
    const auto& hexGrid = _hexGrid->grid();
    for (int i = 0; i < static_cast<int>(hexGrid.size()); ++i) {
        if (hexGrid[i].position() == hexPosition) {
            spdlog::trace("ViewportController::worldPosToHexIndex: world({:.1f}, {:.1f}) -> hex({})",
                          worldPos.x, worldPos.y, i);
            return i;
        }
    }
    
    return -1;
}

int ViewportController::worldPosToTileIndex(sf::Vector2f worldPos, bool isRoof) const {
    // Use hex-based approach for consistency with tile selection
    // Adjust world position for roof offset if selecting roof tiles
    sf::Vector2f adjustedWorldPos = worldPos;
    if (isRoof) {
        adjustedWorldPos.y += ROOF_OFFSET;  // Roof tiles are visually offset upward
    }
    
    int hexIndex = worldPosToHexIndex(adjustedWorldPos);
    if (hexIndex < 0) {
        spdlog::debug("ViewportController::worldPosToTileIndex: No hex found at world({:.1f}, {:.1f}) adjusted({:.1f}, {:.1f}) [roof: {}]",
                      worldPos.x, worldPos.y, adjustedWorldPos.x, adjustedWorldPos.y, isRoof);
        return -1;
    }
    
    // Convert hex coordinates to tile coordinates
    int hexX = hexIndex % HexagonGrid::GRID_WIDTH;  // 0-199
    int hexY = hexIndex / HexagonGrid::GRID_WIDTH;  // 0-199
    int tileX = hexX / 2;  // 0-99
    int tileY = hexY / 2;  // 0-99
    int tileIndex = tileY * MAP_WIDTH + tileX;
    
    spdlog::debug("ViewportController::worldPosToTileIndex: world({:.1f}, {:.1f}) adjusted({:.1f}, {:.1f}) -> hex({}) -> tile({}) [roof: {}]",
                  worldPos.x, worldPos.y, adjustedWorldPos.x, adjustedWorldPos.y, hexIndex, tileIndex, isRoof);
    
    return tileIndex;
}


sf::Vector2f ViewportController::snapToHexGrid(sf::Vector2f worldPos) const {
    if (!_hexGrid) {
        return worldPos;
    }
    
    // Find closest hex and return its center position
    int hexIndex = worldPosToHexIndex(worldPos);
    if (hexIndex >= 0 && hexIndex < static_cast<int>(_hexGrid->grid().size())) {
        const auto& hex = _hexGrid->grid()[hexIndex];
        return sf::Vector2f(static_cast<float>(hex.x()), static_cast<float>(hex.y()));
    }
    
    return worldPos; // Return original position if no valid hex found
}

void ViewportController::updateViewForWindowSize(sf::Vector2u windowSize) {
    if (windowSize.x == 0 || windowSize.y == 0) {
        spdlog::warn("ViewportController: Invalid window size {}x{}", windowSize.x, windowSize.y);
        return;
    }
    
    // Use full viewport (no black bars)
    sf::FloatRect viewport({0, 0}, {1, 1});
    _view.setViewport(viewport);
    
    // Scale view size to match window aspect ratio while maintaining proportional scaling
    // Base reference size is 800x600
    float baseWidth = 800.0f;
    float baseHeight = 600.0f;
    float baseAspect = baseWidth / baseHeight;
    
    // Calculate window aspect ratio
    float windowAspect = static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y);
    
    float viewWidth, viewHeight;
    
    if (windowAspect > baseAspect) {
        // Window is wider - scale to match window width, adjust height proportionally
        viewHeight = baseHeight;
        viewWidth = viewHeight * windowAspect;
    } else {
        // Window is taller or same aspect - scale to match window height, adjust width proportionally  
        viewWidth = baseWidth;
        viewHeight = viewWidth / windowAspect;
    }
    
    _view.setSize(sf::Vector2f(viewWidth, viewHeight));
    
    spdlog::debug("ViewportController: Updated view for {}x{} window - view size: {:.1f}x{:.1f} (aspect: {:.3f})",
                  windowSize.x, windowSize.y, viewWidth, viewHeight, windowAspect);
}

std::optional<HexPosition> ViewportController::worldPosToHexPosition(const WorldCoords& worldPos) const {
    int hexIndex = worldPosToHexIndex(worldPos.toVector());
    if (hexIndex >= 0 && isValidHexPosition(hexIndex)) {
        return HexPosition(hexIndex);
    }
    return std::nullopt;
}

WorldCoords ViewportController::snapToHexGrid(const WorldCoords& worldPos) const {
    sf::Vector2f snapped = snapToHexGrid(worldPos.toVector());
    return WorldCoords(snapped.x, snapped.y);
}

} // namespace geck