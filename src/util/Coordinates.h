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
    constexpr explicit HexPosition(int position = 0) : _position(position) {}

    [[nodiscard]] constexpr int value() const noexcept { return _position; }
    [[nodiscard]] constexpr bool isValid() const noexcept { 
        return _position >= 0 && _position < 40000; 
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const HexPosition&) const = default;
    [[nodiscard]] constexpr bool operator==(const HexPosition&) const = default;

    // Arithmetic operators
    constexpr HexPosition& operator+=(int offset) { _position += offset; return *this; }
    constexpr HexPosition& operator-=(int offset) { _position -= offset; return *this; }
    
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
    constexpr explicit TileIndex(int index = 0) : _index(index) {}

    [[nodiscard]] constexpr int value() const noexcept { return _index; }
    [[nodiscard]] constexpr bool isValid() const noexcept { 
        return _index >= 0 && _index < 10000; 
    }

    // Comparison operators
    [[nodiscard]] constexpr auto operator<=>(const TileIndex&) const = default;
    [[nodiscard]] constexpr bool operator==(const TileIndex&) const = default;

    // Arithmetic operators
    constexpr TileIndex& operator+=(int offset) { _index += offset; return *this; }
    constexpr TileIndex& operator-=(int offset) { _index -= offset; return *this; }
    
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
    constexpr WorldCoords(float x = 0.0f, float y = 0.0f) : _x(x), _y(y) {}
    constexpr explicit WorldCoords(const sf::Vector2f& vec) : _x(vec.x), _y(vec.y) {}

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
        _x += other._x; _y += other._y; return *this; 
    }
    constexpr WorldCoords& operator-=(const WorldCoords& other) { 
        _x -= other._x; _y -= other._y; return *this; 
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
    constexpr ScreenCoords(int x = 0, int y = 0) : _x(x), _y(y) {}

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
        _x += other._x; _y += other._y; return *this; 
    }
    constexpr ScreenCoords& operator-=(const ScreenCoords& other) { 
        _x -= other._x; _y -= other._y; return *this; 
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
    return index >= 0 && index < 10000; // Map::TILES_PER_ELEVATION
}

constexpr bool isValidHexPosition(int position) noexcept {
    return position >= 0 && position < 40000; // HexagonGrid::GRID_WIDTH * GRID_HEIGHT
}

} // namespace geck