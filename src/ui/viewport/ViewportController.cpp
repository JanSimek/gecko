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

void ViewportController::initialize([[maybe_unused]] sf::Vector2u windowSize) {
    // Use fixed view size for consistent coordinate system
    _view.setSize(sf::Vector2f(800.0f, 600.0f));
    
    // Center the view on the map initially
    centerViewOnMap();
    
    spdlog::debug("ViewportController: Initialized with fixed view size {}x{}", 
                  800, 600);
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
    
    spdlog::debug("ViewportController: Set zoom level to {:.2f}", _zoomLevel);
}

int ViewportController::updateHoverHex(sf::Vector2f worldPos) {
    return worldPosToHexIndex(worldPos);
}

int ViewportController::worldPosToHexIndex(sf::Vector2f worldPos) const {
    if (!_hexGrid) {
        return -1;
    }
    
    // Find the closest hex by checking distance to each hex center
    int closestHex = -1;
    float minDistance = std::numeric_limits<float>::max();
    
    const auto& hexGrid = _hexGrid->grid();
    for (int i = 0; i < static_cast<int>(hexGrid.size()); ++i) {
        const auto& hex = hexGrid[i];
        float dx = worldPos.x - static_cast<float>(hex.x());
        float dy = worldPos.y - static_cast<float>(hex.y());
        float distance = dx * dx + dy * dy; // squared distance is sufficient for comparison
        
        if (distance < minDistance) {
            minDistance = distance;
            closestHex = i;
        }
    }
    
    return closestHex;
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

} // namespace geck