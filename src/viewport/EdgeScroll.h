#pragma once

#include <SFML/System/Vector2.hpp>

namespace geck {

/**
 * @brief Pure geometry for cursor-at-edge auto-scroll ("edge scrolling").
 *
 * When the cursor rests near a viewport edge the view should pan that way, the
 * way both reference Fallout 2 mappers do. This holds only the maths: given the
 * cursor's pixel position inside the viewport and the viewport's pixel size, it
 * returns the screen-space scroll velocity (pixels/second) the camera should
 * move at this instant. The caller scales by frame time and converts to world
 * units.
 *
 * The magnitude ramps linearly from 0 at the inner edge of the margin band to
 * @p speedPxPerSec at the viewport edge, so the closer the cursor is to an edge
 * the faster it scrolls; a corner scrolls diagonally. The result is the zero
 * vector when the cursor sits outside the margin band, or off the viewport
 * entirely (pixel coordinates are half-open [0, size), so the right/bottom edges
 * are exclusive).
 *
 * Deliberately free of Qt and any render target so it stays headless-unit-testable.
 */
struct EdgeScroll {
    /// Distance (px) from an edge within which the cursor triggers scrolling.
    static constexpr float DEFAULT_MARGIN = 32.0f;
    /// Peak scroll speed at the very edge, in screen pixels per second.
    static constexpr float DEFAULT_SPEED = 900.0f;

    /**
     * @brief Screen-space scroll velocity for the current cursor position.
     * @param cursorPx      Cursor position inside the viewport, in pixels.
     * @param viewportPx    Viewport size in pixels.
     * @param marginPx      Edge band width in pixels (<= 0 disables scrolling).
     * @param speedPxPerSec Peak speed at the edge, screen pixels per second.
     * @return Velocity in screen pixels/second; zero when no edge is engaged.
     */
    [[nodiscard]] static sf::Vector2f velocity(sf::Vector2f cursorPx,
        sf::Vector2f viewportPx,
        float marginPx,
        float speedPxPerSec);
};

} // namespace geck
