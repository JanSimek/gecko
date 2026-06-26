#pragma once

#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cstddef>
#include <vector>

namespace geck::geometry {

/**
 * @brief Standard even-odd (ray-casting) point-in-polygon test.
 *
 * Returns true when @p point lies inside the polygon described by @p vertices (an open ring;
 * the closing edge from the last vertex back to the first is implied). The polygon may be
 * convex or concave and the winding direction does not matter. A polygon with fewer than three
 * vertices encloses no area, so the result is always false.
 *
 * Points exactly on an edge are reported consistently by the half-open crossing rule below, but
 * (as with any floating-point even-odd test) callers should not rely on a specific answer for a
 * point that sits exactly on an edge — the editor only ever tests hex *centers*, which never
 * coincide with a user-drawn polygon edge in practice.
 *
 * Free function with no engine dependencies so the geometry is unit-testable headlessly.
 */
inline bool pointInPolygon(sf::Vector2f point, const std::vector<sf::Vector2f>& vertices) {
    const std::size_t count = vertices.size();
    if (count < 3) {
        return false;
    }

    bool inside = false;
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const sf::Vector2f& a = vertices[i];
        const sf::Vector2f& b = vertices[j];

        // The horizontal ray from `point` going +x crosses edge (b -> a) when the edge straddles
        // point.y (half-open in y, so a shared vertex is counted exactly once) and the crossing
        // x lies to the right of point.x.
        const bool straddlesY = (a.y > point.y) != (b.y > point.y);
        if (straddlesY) {
            const float crossX = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
            if (point.x < crossX) {
                inside = !inside;
            }
        }
    }
    return inside;
}

/**
 * @brief Axis-aligned bounding box of a polygon's vertices.
 *
 * Returns {minX, minY, maxX, maxY}. For an empty vertex list every component is zero. Used to cheaply
 * cull which hexes to ray-cast against (only those whose center is inside the bbox).
 */
struct BoundingBox {
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;

    [[nodiscard]] bool contains(sf::Vector2f point) const {
        return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
    }
};

inline BoundingBox polygonBounds(const std::vector<sf::Vector2f>& vertices) {
    BoundingBox box;
    if (vertices.empty()) {
        return box;
    }
    box.minX = box.maxX = vertices.front().x;
    box.minY = box.maxY = vertices.front().y;
    for (const sf::Vector2f& v : vertices) {
        box.minX = std::min(box.minX, v.x);
        box.minY = std::min(box.minY, v.y);
        box.maxX = std::max(box.maxX, v.x);
        box.maxY = std::max(box.maxY, v.y);
    }
    return box;
}

} // namespace geck::geometry
