#include "ViewportController.h"
#include "editor/Hex.h"
#include "util/Constants.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

using namespace geck;

namespace geck {

ViewportController::ViewportController(const HexagonGrid* hexGrid)
    : _hexGrid(hexGrid) {
}

void ViewportController::initialize(sf::Vector2u windowSize) {
    updateViewForWindowSize(windowSize);
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
        newZoom *= (1.0f + ZOOM_STEP);
    } else if (direction < 0) {
        newZoom *= (1.0f - ZOOM_STEP);
    }

    setZoomLevel(newZoom);
}

void ViewportController::setZoomLevel(float zoom) {
    const float clamped = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    if (std::abs(clamped - _zoomLevel) < 0.0001f) {
        return;
    }

    const sf::Vector2f center = _view.getCenter();
    _zoomLevel = clamped;

    const float width = static_cast<float>(_windowSize.x) / _zoomLevel;
    const float height = static_cast<float>(_windowSize.y) / _zoomLevel;

    _view.setSize({ width, height });
    if (center != sf::Vector2f()) {
        _view.setCenter(center);
    }
}

int ViewportController::updateHoverHex(sf::Vector2f worldPos) {
    return worldPosToHexIndex(worldPos);
}

int ViewportController::worldPosToHexIndex(sf::Vector2f worldPos) const {
    if (!_hexGrid) {
        return -1;
    }

    uint32_t hexPosition = _hexGrid->positionAt(static_cast<uint32_t>(worldPos.x), static_cast<uint32_t>(worldPos.y));

    if (hexPosition == Hex::HEX_OUT_OF_MAP) {
        spdlog::debug("ViewportController::worldPosToHexIndex: world({:.1f}, {:.1f}) -> out of map",
            worldPos.x, worldPos.y);
        return -1;
    }

    const int hexIndex = static_cast<int>(hexPosition);
    if (_hexGrid->containsPosition(hexIndex)) {
        spdlog::trace("ViewportController::worldPosToHexIndex: world({:.1f}, {:.1f}) -> hex({})",
            worldPos.x, worldPos.y, hexIndex);
        return hexIndex;
    }

    return -1;
}

bool ViewportController::isHexVisible(int hexWorldX, int hexWorldY, const sf::View& view) {
    // A hex sprite spans ~2 hex-widths and sits a few px below its baseline, so pad
    // the right/bottom test by those amounts to avoid culling partially-visible hexes.
    constexpr int HEX_SPRITE_WIDTH_SPAN = Hex::HEX_WIDTH * 2;
    constexpr int HEX_BASELINE_OFFSET = 4;

    const sf::Vector2f viewCenter = view.getCenter();
    const sf::Vector2f viewSize = view.getSize();
    const int worldX = static_cast<int>(viewCenter.x - viewSize.x / 2);
    const int worldY = static_cast<int>(viewCenter.y - viewSize.y / 2);
    const int viewWidth = static_cast<int>(viewSize.x);
    const int viewHeight = static_cast<int>(viewSize.y);

    return (hexWorldX + HEX_SPRITE_WIDTH_SPAN > worldX && hexWorldX < worldX + viewWidth)
        && (hexWorldY + Hex::HEX_HEIGHT + HEX_BASELINE_OFFSET > worldY && hexWorldY < worldY + viewHeight);
}

sf::Vector2f ViewportController::snapToHexGrid(sf::Vector2f worldPos) const {
    if (!_hexGrid) {
        return worldPos;
    }

    // Find closest hex and return its center position
    int hexIndex = worldPosToHexIndex(worldPos);
    if (auto hex = _hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex)); hex.has_value()) {
        return sf::Vector2f(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
    }

    return worldPos; // Return original position if no valid hex found
}

void ViewportController::updateViewForWindowSize(sf::Vector2u windowSize) {
    if (windowSize.x == 0 || windowSize.y == 0) {
        spdlog::warn("ViewportController: Invalid window size {}x{}", windowSize.x, windowSize.y);
        return;
    }

    _windowSize = windowSize;

    const sf::Vector2f center = _view.getCenter();

    const float width = static_cast<float>(_windowSize.x) / _zoomLevel;
    const float height = static_cast<float>(_windowSize.y) / _zoomLevel;

    _view.setViewport(sf::FloatRect({ 0.f, 0.f }, { 1.f, 1.f }));
    _view.setSize({ width, height });
    if (center != sf::Vector2f()) {
        _view.setCenter(center);
    }

    spdlog::debug("ViewportController: Resize {}x{} -> view {:.1f}x{:.1f} (zoom {:.2f})",
        windowSize.x, windowSize.y, width, height, _zoomLevel);
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
