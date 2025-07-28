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

} // namespace geck