#include <algorithm>
#include <numeric>

#include "Direction.h"
#include "Frame.h"

namespace geck {

int16_t Direction::shiftX() const {
    return _shiftX;
}

void Direction::setShiftX(const int16_t& shiftX) {
    _shiftX = shiftX;
}

int16_t Direction::shiftY() const {
    return _shiftY;
}

void Direction::setShiftY(const int16_t& shiftY) {
    _shiftY = shiftY;
}

uint32_t Direction::dataOffset() const {
    return _dataOffset;
}

void Direction::setDataOffset(const uint32_t& dataOffset) {
    _dataOffset = dataOffset;
}

const std::vector<Frame>& Direction::frames() const {
    return _frames;
}

void Direction::setFrames(std::vector<Frame> frames) {
    _frames = std::move(frames);
}

/**
 * @brief Calculates the width by taking the widest frame and multiplying it by the number of frames
 * @return width of all frames resized to the width of the widest frame
 */
uint16_t Direction::width() const {
    auto widest = std::max_element(_frames.begin(), _frames.end(), [](const Frame& a, const Frame& b) {
        return a.width() < b.width();
    });

    return widest->width() * static_cast<uint16_t>(_frames.size());
}

/**
 * @brief Finds the height of the tallest frame (frames are stacked horizontally)
 * @return height of the tallest frame
 */
uint16_t Direction::height() const {
    auto tallest = std::max_element(_frames.begin(), _frames.end(), [](const Frame& a, const Frame& b) {
        return a.height() < b.height();
    });

    return tallest->height();
}

} // namespace geck
