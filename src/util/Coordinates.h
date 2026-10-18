#pragma once

#include <SFML/System/Vector2.hpp>
#include <compare>
#include <cstdint>

#include "util/Constants.h"

namespace geck {

/**
 * @brief Type-safe hex position coordinate
 *
 * Represents a position in the hex grid (0-39,999 for Fallout 2 maps)
 * Provides type safety to prevent mixing with tile coordinates.
 */
class HexPosition {
public:
    static constexpr int MAX_VALUE = 39999; // 0-39999 range for 200x200 hex grid

    constexpr explicit HexPosition(int position = 0)
        : _position(position) { }

    [[nodiscard]] constexpr int value() const noexcept { return _position; }
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return _position >= 0 && _position <= MAX_VALUE;
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const HexPosition&) const = default;
    [[nodiscard]] constexpr bool operator==(const HexPosition&) const = default;

    // Arithmetic operators
    constexpr HexPosition& operator+=(int offset) {
        _position += offset;
        return *this;
    }
    constexpr HexPosition& operator-=(int offset) {
        _position -= offset;
        return *this;
    }

    [[nodiscard]] constexpr HexPosition operator+(int offset) const {
        return HexPosition(_position + offset);
    }
    [[nodiscard]] constexpr HexPosition operator-(int offset) const {
        return HexPosition(_position - offset);
    }

private:
    int _position;
};

/**
 * @brief Type-safe world coordinates
 *
 * Represents floating-point world coordinates with explicit conversion
 * to prevent accidental truncation.
 */
class WorldCoords {
public:
    constexpr WorldCoords(float x = 0.0f, float y = 0.0f)
        : _x(x)
        , _y(y) { }
    constexpr explicit WorldCoords(const sf::Vector2f& vec)
        : _x(vec.x)
        , _y(vec.y) { }

    [[nodiscard]] constexpr float x() const noexcept { return _x; }
    [[nodiscard]] constexpr float y() const noexcept { return _y; }

    [[nodiscard]] constexpr sf::Vector2f toVector() const noexcept {
        return sf::Vector2f(_x, _y);
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const WorldCoords&) const = default;
    [[nodiscard]] constexpr bool operator==(const WorldCoords&) const = default;

    // Arithmetic operators
    constexpr WorldCoords& operator+=(const WorldCoords& other) {
        _x += other._x;
        _y += other._y;
        return *this;
    }
    constexpr WorldCoords& operator-=(const WorldCoords& other) {
        _x -= other._x;
        _y -= other._y;
        return *this;
    }

    [[nodiscard]] constexpr WorldCoords operator+(const WorldCoords& other) const {
        return WorldCoords(_x + other._x, _y + other._y);
    }
    [[nodiscard]] constexpr WorldCoords operator-(const WorldCoords& other) const {
        return WorldCoords(_x - other._x, _y - other._y);
    }

private:
    float _x, _y;
};

// Helper functions for tile validation
constexpr bool isValidTileIndex(int index) noexcept {
    return index >= 0 && index < TILES_PER_ELEVATION;
}

constexpr bool isValidHexPosition(int position) noexcept {
    return position >= 0 && position <= HexPosition::MAX_VALUE;
}

} // namespace geck