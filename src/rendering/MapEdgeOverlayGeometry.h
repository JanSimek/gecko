#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include "editor/HexagonGrid.h"
#include "format/map/MapEdge.h"
#include "util/Constants.h"
#include "util/TileUtils.h"

namespace geck {

/// @file
/// Pure geometry for drawing the `.edg` map-edge overlay, factored out of RenderingEngine so it can
/// be unit-tested without a GL context. A zone's `tileRect` holds four **hex indices** (0..39999);
/// the v2 `squareRect` holds `(col, row)` pairs in the 100x100 floor grid.

/// World-space axis-aligned bounds of a zone. `left`/`right` give the X extent, `top`/`bottom` the
/// Y extent — mirroring the engine's `tileRectToScreenRect` (map_edge_setup.cc). Returns nullopt if
/// any corner hex is off-grid.
inline std::optional<sf::FloatRect> mapEdgeZoneWorldBounds(const HexagonGrid& grid,
    const MapEdge::Rect& rect) {
    auto worldOf = [&](int hexIndex) -> std::optional<sf::Vector2f> {
        if (hexIndex < 0) {
            return std::nullopt;
        }
        const auto hex = grid.getHexByPosition(static_cast<uint32_t>(hexIndex));
        if (!hex.has_value()) {
            return std::nullopt;
        }
        return sf::Vector2f(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));
    };

    const auto left = worldOf(rect.left);
    const auto right = worldOf(rect.right);
    const auto top = worldOf(rect.top);
    const auto bottom = worldOf(rect.bottom);
    if (!left || !right || !top || !bottom) {
        return std::nullopt;
    }

    const float minX = std::min(left->x, right->x);
    const float maxX = std::max(left->x, right->x);
    const float minY = std::min(top->y, bottom->y);
    const float maxY = std::max(top->y, bottom->y);
    return sf::FloatRect({ minX, minY }, { maxX - minX, maxY - minY });
}

/// World-space centre of a square-grid cell `(col, row)` in the 100x100 floor grid.
inline sf::Vector2f mapEdgeSquareCellCentre(int col, int row) {
    const int index = row * static_cast<int>(MAP_WIDTH) + col;
    const auto screen = indexToScreenPosition(index);
    // Centre of the tile diamond (corners are +48/+80/+32/+0 off the top-left; see
    // RenderingEngine::renderTileSelectionOutline).
    return sf::Vector2f(static_cast<float>(screen.x) + 40.f, static_cast<float>(screen.y) + 18.f);
}

/// The four world-space corners of a v2 `squareRect` (`left`/`right` are columns, `top`/`bottom`
/// rows), in the engine's vertex order: 0=(right,top) 1=(left,top) 2=(left,bottom) 3=(right,bottom)
/// (map_edge_setup.cc `squareRectScreenCorners`). Consecutive corners bound the top/left/bottom/right
/// sides in turn.
inline std::array<sf::Vector2f, 4> mapEdgeSquareCorners(const MapEdge::Rect& square) {
    return {
        mapEdgeSquareCellCentre(square.right, square.top),
        mapEdgeSquareCellCentre(square.left, square.top),
        mapEdgeSquareCellCentre(square.left, square.bottom),
        mapEdgeSquareCellCentre(square.right, square.bottom),
    };
}

} // namespace geck
