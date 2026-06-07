#include "Frame.h"
#include "../pal/Pal.h"

namespace geck {

Frame::Frame(uint16_t width, uint16_t height)
    : _width(width)
    , _height(height)
    , _indexes(width * height, 0) { }

uint16_t Frame::width() const {
    return _width;
}

void Frame::setWidth(const uint16_t& width) {
    _width = width;
}

uint16_t Frame::height() const {
    return _height;
}

void Frame::setHeight(const uint16_t& height) {
    _height = height;
}

int16_t Frame::offsetX() const {
    return _offsetX;
}

void Frame::setOffsetX(const int16_t& offsetX) {
    _offsetX = offsetX;
}

int16_t Frame::offsetY() const {
    return _offsetY;
}

void Frame::setOffsetY(const int16_t& offsetY) {
    _offsetY = offsetY;
}

uint8_t Frame::index(uint16_t x, uint16_t y) const {
    if (x >= _width || y >= _height)
        return 0;

    return _indexes.at(static_cast<std::size_t>(_width * y + x));
}

uint8_t* Frame::data() {
    return _indexes.data();
}

uint8_t* Frame::rgba(Pal* pal) {
    const auto pixelCount = width() * height() * RGBA;
    const auto& colors = pal->palette();
    const auto& colorIndexes = _indexes.data();

    _rgba.reserve(pixelCount);

    for (int i = 0; i < pixelCount; i += RGBA) {
        uint8_t paletteIndex = colorIndexes[i / RGBA];

        geck::Rgb color = colors[paletteIndex];
        constexpr uint8_t white = 255;
        constexpr uint8_t opaque_alpha = 255;

        if (color.r == white && color.g == white && color.b == white) {
            // transparent
            _rgba.emplace_back(0);
            _rgba.emplace_back(0);
            _rgba.emplace_back(0);
            _rgba.emplace_back(0);
        } else {
            constexpr int brightness = 4;
            _rgba.emplace_back(color.r * brightness);
            _rgba.emplace_back(color.g * brightness);
            _rgba.emplace_back(color.b * brightness);
            _rgba.emplace_back(opaque_alpha);
        }
    }
    return _rgba.data();
}

} // namespace geck
