#pragma once

#include <cstdint>
#include <vector>

namespace geck {

class Pal;

class Frame {
private:
    uint16_t _width = 0;
    uint16_t _height = 0;
    int16_t _offsetX = 0;
    int16_t _offsetY = 0;
    std::vector<uint8_t> _indexes;
    mutable std::vector<uint8_t> _rgba; // lazily built RGBA cache (see rgba())

public:
    static constexpr int RGBA = 4; // RGBA of SFML texture

    Frame(uint16_t width, uint16_t height);

    uint16_t width() const;
    void setWidth(const uint16_t& width);

    uint16_t height() const;
    void setHeight(const uint16_t& height);

    int16_t offsetX() const;
    void setOffsetX(const int16_t& offsetX);

    int16_t offsetY() const;
    void setOffsetY(const int16_t& offsetY);

    uint8_t index(uint16_t x, uint16_t y) const;

    uint8_t* data();

    uint8_t* rgba(const Pal* pal) const;
};

} // namespace geck
