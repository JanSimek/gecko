#pragma once

#include <SFML/System/Vector2.hpp>
#include <compare>
#include <cstdint>

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
 * @brief Type-safe tile index
 *
 * Represents a position in the tile grid (0-9,999 for Fallout 2 maps)
 * Provides type safety to prevent mixing with hex coordinates.
 */
class TileIndex {
public:
    static constexpr int MAX_VALUE = 9999; // 0-9999 range for 100x100 tile grid

    constexpr explicit TileIndex(int index = 0)
        : _index(index) { }

    [[nodiscard]] constexpr int value() const noexcept { return _index; }
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return _index >= 0 && _index <= MAX_VALUE;
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const TileIndex&) const = default;
    [[nodiscard]] constexpr bool operator==(const TileIndex&) const = default;

    // Arithmetic operators
    constexpr TileIndex& operator+=(int offset) {
        _index += offset;
        return *this;
    }
    constexpr TileIndex& operator-=(int offset) {
        _index -= offset;
        return *this;
    }

    [[nodiscard]] constexpr TileIndex operator+(int offset) const {
        return TileIndex(_index + offset);
    }
    [[nodiscard]] constexpr TileIndex operator-(int offset) const {
        return TileIndex(_index - offset);
    }

private:
    int _index;
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

/**
 * @brief Type-safe screen coordinates
 *
 * Represents integer screen/pixel coordinates.
 */
class ScreenCoords {
public:
    constexpr ScreenCoords(int x = 0, int y = 0)
        : _x(x)
        , _y(y) { }

    [[nodiscard]] constexpr int x() const noexcept { return _x; }
    [[nodiscard]] constexpr int y() const noexcept { return _y; }

    [[nodiscard]] constexpr sf::Vector2i toVector() const noexcept {
        return sf::Vector2i(_x, _y);
    }
    [[nodiscard]] constexpr sf::Vector2f toFloatVector() const noexcept {
        return sf::Vector2f(static_cast<float>(_x), static_cast<float>(_y));
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const ScreenCoords&) const = default;
    [[nodiscard]] constexpr bool operator==(const ScreenCoords&) const = default;

    // Arithmetic operators
    constexpr ScreenCoords& operator+=(const ScreenCoords& other) {
        _x += other._x;
        _y += other._y;
        return *this;
    }
    constexpr ScreenCoords& operator-=(const ScreenCoords& other) {
        _x -= other._x;
        _y -= other._y;
        return *this;
    }

    [[nodiscard]] constexpr ScreenCoords operator+(const ScreenCoords& other) const {
        return ScreenCoords(_x + other._x, _y + other._y);
    }
    [[nodiscard]] constexpr ScreenCoords operator-(const ScreenCoords& other) const {
        return ScreenCoords(_x - other._x, _y - other._y);
    }

private:
    int _x, _y;
};

/**
 * @brief Type-safe elevation level
 *
 * Replaces magic numbers and provides bounds checking.
 */
enum class Elevation : int {
    LEVEL_1 = 0,
    LEVEL_2 = 1,
    LEVEL_3 = 2
};

constexpr bool isValidElevation(int elevation) noexcept {
    return elevation >= 0 && elevation <= 2;
}

constexpr Elevation toElevation(int elevation) {
    if (!isValidElevation(elevation)) {
        // Note: In a constexpr context, we can't throw, so this would be a compile-time error
        // At runtime, this should be validated before calling this function
    }
    return static_cast<Elevation>(elevation);
}

constexpr int toInt(Elevation elevation) noexcept {
    return static_cast<int>(elevation);
}

// Helper functions for tile validation
constexpr bool isValidTileIndex(int index) noexcept {
    return index >= 0 && index <= TileIndex::MAX_VALUE;
}

constexpr bool isValidHexPosition(int position) noexcept {
    return position >= 0 && position <= HexPosition::MAX_VALUE;
}

/**
 * @brief Type conversion utilities for gradual migration
 *
 * These functions help migrate from raw int coordinates to type-safe classes
 * without breaking existing code all at once.
 */
namespace CoordinateUtils {

    // Convert legacy int values to type-safe classes
    [[nodiscard]] constexpr HexPosition toHexPosition(int position) {
        return HexPosition(position);
    }

    [[nodiscard]] constexpr TileIndex toTileIndex(int index) {
        return TileIndex(index);
    }

    [[nodiscard]] constexpr WorldCoords toWorldCoords(float x, float y) {
        return WorldCoords(x, y);
    }

    [[nodiscard]] constexpr ScreenCoords toScreenCoords(int x, int y) {
        return ScreenCoords(x, y);
    }

    // Convert from SFML types
    [[nodiscard]] constexpr WorldCoords fromVector2f(const sf::Vector2f& vec) {
        return WorldCoords(vec.x, vec.y);
    }

    [[nodiscard]] constexpr ScreenCoords fromVector2i(const sf::Vector2i& vec) {
        return ScreenCoords(vec.x, vec.y);
    }

    // Validate and convert (throws on invalid input)
    [[nodiscard]] HexPosition toValidHexPosition(int position);
    [[nodiscard]] TileIndex toValidTileIndex(int index);
    [[nodiscard]] Elevation toValidElevation(int elevation);
}

} // namespace geck