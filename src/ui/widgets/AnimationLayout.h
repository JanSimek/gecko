#pragma once

#include <algorithm>
#include <limits>
#include <vector>

namespace geck {

/// One FRM frame's size plus its signed per-frame (x, y) offset, in FRM order.
struct FrameBox {
    int offsetX = 0;
    int offsetY = 0;
    int width = 0;
    int height = 0;
};

/// Where a frame is drawn on the shared canvas (top-left, in canvas pixels).
struct FrameRect {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

/// A common canvas size plus each frame's draw position within it.
struct AnimationLayout {
    int canvasWidth = 0;
    int canvasHeight = 0;
    std::vector<FrameRect> placements; // parallel to the input frames
    bool valid() const { return canvasWidth > 0 && canvasHeight > 0; }
};

/// Upper bound on a laid-out canvas dimension. Real FRM sprites are at most a few
/// hundred pixels; anything larger means a malformed/corrupt FRM (e.g. wild
/// offsets), and we refuse it rather than attempt a huge pixmap allocation.
inline constexpr int MAX_ANIMATION_CANVAS_DIMENSION = 2048;

/// Lays out an FRM animation onto a single fixed-size canvas.
///
/// FRM frames differ in size and carry signed per-frame offsets that the engine
/// accumulates as the animation advances, keeping the sprite anchored at its
/// bottom-centre (fallout2-ce object.cc blits at left = x - width/2,
/// top = y - height + 1 with the accumulated offset added). Centring each frame's
/// own bounding box independently makes the sprite wobble; this returns one canvas
/// big enough for every anchored frame and each frame's position within it, so a
/// preview can composite them and play back without jitter.
///
/// Empty frames (zero width or height) keep their slot in `placements` but do not
/// size the canvas. If no frame is renderable the result is invalid() (0x0).
inline AnimationLayout computeAnimationLayout(const std::vector<FrameBox>& frames) {
    AnimationLayout layout;
    layout.placements.reserve(frames.size());

    int cumX = 0;
    int cumY = 0;
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    // First pass: accumulate offsets and place each frame relative to the anchor.
    std::vector<FrameRect> raw;
    raw.reserve(frames.size());
    for (const auto& frame : frames) {
        cumX += frame.offsetX;
        cumY += frame.offsetY;
        const int left = cumX - frame.width / 2;
        const int top = cumY - frame.height + 1;
        raw.push_back({ left, top, frame.width, frame.height });
        if (frame.width == 0 || frame.height == 0) {
            continue; // do not size the canvas to empty frames
        }
        minX = std::min(minX, left);
        minY = std::min(minY, top);
        maxX = std::max(maxX, left + frame.width);
        maxY = std::max(maxY, top + frame.height);
    }

    if (maxX <= minX || maxY <= minY) {
        return layout; // no renderable frames -> invalid 0x0 layout
    }

    const int width = maxX - minX;
    const int height = maxY - minY;
    if (width > MAX_ANIMATION_CANVAS_DIMENSION || height > MAX_ANIMATION_CANVAS_DIMENSION) {
        return layout; // implausibly large -> treat as invalid, do not allocate
    }

    layout.canvasWidth = width;
    layout.canvasHeight = height;

    // Second pass: shift every placement into canvas-local coordinates.
    for (const auto& r : raw) {
        layout.placements.push_back({ r.left - minX, r.top - minY, r.width, r.height });
    }
    return layout;
}

} // namespace geck
