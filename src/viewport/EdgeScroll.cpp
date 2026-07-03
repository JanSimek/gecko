#include "EdgeScroll.h"

#include <algorithm>

namespace geck {

sf::Vector2f EdgeScroll::velocity(sf::Vector2f cursorPx,
    sf::Vector2f viewportPx,
    float marginPx,
    float speedPxPerSec) {
    // Degenerate viewport or a disabled margin: nothing to scroll.
    if (viewportPx.x <= 0.f || viewportPx.y <= 0.f || marginPx <= 0.f) {
        return {};
    }

    // A cursor off the viewport does not scroll (the caller only asks while it hovers,
    // but stay robust to a cursor sitting exactly on/just past an edge).
    if (cursorPx.x < 0.f || cursorPx.y < 0.f || cursorPx.x > viewportPx.x || cursorPx.y > viewportPx.y) {
        return {};
    }

    // Ramp factor in [0,1]: 0 at (or outside) the margin boundary, 1 at the edge.
    const auto ramp = [marginPx](float distanceFromEdge) -> float {
        if (distanceFromEdge >= marginPx) {
            return 0.f;
        }
        return std::clamp((marginPx - distanceFromEdge) / marginPx, 0.f, 1.f);
    };

    sf::Vector2f velocity{ 0.f, 0.f };

    const float leftRamp = ramp(cursorPx.x);
    const float rightRamp = ramp(viewportPx.x - cursorPx.x);
    if (leftRamp > 0.f) {
        velocity.x = -leftRamp * speedPxPerSec;
    } else if (rightRamp > 0.f) {
        velocity.x = rightRamp * speedPxPerSec;
    }

    const float topRamp = ramp(cursorPx.y);
    const float bottomRamp = ramp(viewportPx.y - cursorPx.y);
    if (topRamp > 0.f) {
        velocity.y = -topRamp * speedPxPerSec;
    } else if (bottomRamp > 0.f) {
        velocity.y = bottomRamp * speedPxPerSec;
    }

    return velocity;
}

} // namespace geck
