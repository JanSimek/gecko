#include "Frm.h"
#include "Direction.h"
#include "Frame.h"
#include "../pal/Pal.h"

#include <algorithm>

namespace geck {

const std::vector<Direction>& Frm::directions() const {
    return _directions;
}

void Frm::setDirections(std::vector<Direction> directions) {
    _directions = std::move(directions);

    updateFrameDimensions();
}

uint32_t Frm::version() const {
    return _version;
}

void Frm::setVersion(const uint32_t& version) {
    _version = version;
}

uint16_t Frm::fps() const {
    return _fps;
}

void Frm::setFps(const uint16_t& fps) {
    _fps = fps;
}

uint16_t Frm::actionFrame() const {
    return _actionFrame;
}

void Frm::setActionFrame(const uint16_t& actionFrame) {
    _actionFrame = actionFrame;
}

uint16_t Frm::framesPerDirection() const {
    return _framesPerDirection;
}

void Frm::setFramesPerDirection(const uint16_t& framesPerDirection) {
    _framesPerDirection = framesPerDirection;
}

/**
 * @brief Calculates total FRM width by multiplying the widest direction by the number of directions
 * @return width of the widest direction
 */
uint16_t Frm::width() const {
    auto widest = std::max_element(_directions.begin(), _directions.end(), [](const Direction& a, const Direction& b) {
        return a.width() < b.width();
    });

    return widest->width() * static_cast<uint16_t>(_directions.size());
}

/**
 * @brief Calculates total FRM height by multiplying the tallest direction by the number of directions
 * @return height of the tallest direction
 */
uint16_t Frm::height() const {
    auto tallest = std::max_element(_directions.begin(), _directions.end(), [](const Direction& a, const Direction& b) {
        return a.height() < b.height();
    });

    return tallest->height() * static_cast<uint16_t>(_directions.size());
}

void Frm::updateFrameDimensions() {
    _maxFrameWidth = _directions.front().frames().front().width();
    _maxFrameHeight = _directions.front().frames().front().height();

    for (Direction& direction : _directions) {

        for (const Frame& frame : direction.frames()) {
            if (frame.width() > _maxFrameWidth)
                _maxFrameWidth = frame.width();
            if (frame.height() > _maxFrameHeight)
                _maxFrameHeight = frame.height();
        }
    }
}

Frm::Frm(const std::filesystem::path& path)
    : IFile(path) {
}

uint16_t Frm::maxFrameWidth() const {
    return _maxFrameWidth;
}

uint16_t Frm::maxFrameHeight() const {
    return _maxFrameHeight;
}

} // namespace geck
