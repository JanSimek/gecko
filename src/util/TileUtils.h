#pragma once

#include <cmath>
#include <optional>
#include <tuple>
#include <SFML/Graphics.hpp>
#include "Constants.h"
#include "editor/HexagonGrid.h"

namespace geck {

/**
 * @brief Utility functions for tile position calculations and common operations
 */

/**
 * @brief Represents tile coordinates in the grid
 * Supports structured bindings: auto [x, y] = coords;
 */
struct TileCoordinates {
    unsigned int x; ///< X coordinate (row) in the tile grid
    unsigned int y; ///< Y coordinate (column) in the tile grid

    constexpr TileCoordinates(unsigned int initX = 0, unsigned int initY = 0) noexcept
        : x(initX)
        , y(initY) { }

    template <std::size_t I>
    [[nodiscard]] constexpr auto get() const noexcept {
        if constexpr (I == 0)
            return x;
        else if constexpr (I == 1)
            return y;
    }
};

/**
 * @brief Represents screen position in pixels
 * Supports structured bindings: auto [x, y] = screenPos;
 */
struct ScreenPosition {
    unsigned int x; ///< X position in pixels
    unsigned int y; ///< Y position in pixels

    constexpr ScreenPosition(unsigned int posX = 0, unsigned int posY = 0) noexcept
        : x(posX)
        , y(posY) { }

    template <std::size_t I>
    [[nodiscard]] constexpr auto get() const noexcept {
        if constexpr (I == 0)
            return x;
        else if constexpr (I == 1)
            return y;
    }
};

/**
 * @brief Convert linear tile index to tile coordinates
 *
 * @param tileIndex Linear index of the tile (0 to TILES_PER_ELEVATION-1)
 * @return TileCoordinates with x (row) and y (column) values
 */
inline TileCoordinates indexToCoordinates(int tileIndex) {
    return TileCoordinates(
        static_cast<unsigned int>(tileIndex) / MAP_WIDTH,
        static_cast<unsigned int>(tileIndex) % MAP_WIDTH);
}

/**
 * @brief Convert tile coordinates to linear tile index
 *
 * @param coords Tile coordinates
 * @return Linear tile index
 */
inline int coordinatesToIndex(const TileCoordinates& coords) {
    return static_cast<int>(coords.x * MAP_WIDTH + coords.y);
}

/**
 * @brief Calculate screen position from tile coordinates
 *
 * This function implements the isometric projection used by Fallout 2
 * to convert tile grid coordinates to screen pixel coordinates.
 *
 * @param coords Tile coordinates in the grid
 * @param isRoof Whether this is for a roof tile (applies roof offset)
 * @return Screen position in pixels
 */
inline ScreenPosition coordinatesToScreenPosition(const TileCoordinates& coords, bool isRoof = false) {
    unsigned int x = (MAP_WIDTH - coords.y - 1) * TILE_X_OFFSET + TILE_Y_OFFSET_LARGE * coords.x;
    unsigned int y = coords.x * TILE_Y_OFFSET_SMALL + coords.y * TILE_Y_OFFSET_TINY + TILE_Y_OFFSET_TINY;

    if (isRoof) {
        y -= ROOF_OFFSET;
    }

    return ScreenPosition(x, y);
}

/**
 * @brief Calculate screen position directly from tile index
 *
 * Convenience function that combines index-to-coordinates and coordinates-to-screen
 * conversions in a single call.
 *
 * @param tileIndex Linear tile index
 * @param isRoof Whether this is for a roof tile
 * @return Screen position in pixels
 */
inline ScreenPosition indexToScreenPosition(int tileIndex, bool isRoof = false) {
    return coordinatesToScreenPosition(indexToCoordinates(tileIndex), isRoof);
}

/**
 * @brief Resolve a world/screen point to the tile under it (inverse of indexToScreenPosition).
 *
 * Tile centres form a sheared lattice, so the click is resolved by solving the projection's
 * linear system for a fractional (row, col) estimate, then choosing the Euclidean-nearest tile
 * centre in the surrounding 3x3 neighbourhood — i.e. the diamond the point actually falls in.
 * Unlike snapping the click to a hex and converting hex->tile, this stays accurate right at
 * tile boundaries.
 *
 * @param worldX,worldY Click position in the same world space as the tile sprites.
 * @param isRoof Resolve against the roof layer (applies the roof offset).
 * @return Tile index in [0, TILES_PER_ELEVATION), or std::nullopt if the point is off the grid.
 */
inline std::optional<int> screenToTileIndex(float worldX, float worldY, bool isRoof = false) {
    constexpr float X = static_cast<float>(TILE_X_OFFSET);       // column x-step
    constexpr float L = static_cast<float>(TILE_Y_OFFSET_LARGE); // row x-step
    constexpr float S = static_cast<float>(TILE_Y_OFFSET_SMALL); // row y-step
    constexpr float T = static_cast<float>(TILE_Y_OFFSET_TINY);  // column y-step
    constexpr float halfW = TILE_WIDTH / 2.0f;
    constexpr float halfH = TILE_HEIGHT / 2.0f;

    // Roof tiles are drawn ROOF_OFFSET higher; map the click back into floor space.
    const float py = worldY + (isRoof ? static_cast<float>(ROOF_OFFSET) : 0.0f);

    // Tile centre: cx = (W-1-col)*X + L*row + halfW ;  cy = S*row + T*col + T + halfH.
    // Rearranged:  P = L*row - X*col ;  Q = S*row + T*col. Solve the 2x2 system for (row, col).
    const float P = (worldX - halfW) - static_cast<float>(MAP_WIDTH - 1) * X;
    const float Q = py - T - halfH;
    constexpr float det = L * T + X * S;
    const float rowF = (T * P + X * Q) / det;
    const float colF = (L * Q - S * P) / det;

    // Reject points clearly off the grid (allow ~half a tile of margin).
    if (rowF < -1.0f || rowF > MAP_HEIGHT || colF < -1.0f || colF > MAP_WIDTH) {
        return std::nullopt;
    }

    const int rowEstimate = static_cast<int>(std::lround(rowF));
    const int colEstimate = static_cast<int>(std::lround(colF));

    int best = -1;
    float bestDistSq = 0.0f;
    for (int row = rowEstimate - 1; row <= rowEstimate + 1; ++row) {
        for (int col = colEstimate - 1; col <= colEstimate + 1; ++col) {
            if (row < 0 || row >= MAP_HEIGHT || col < 0 || col >= MAP_WIDTH) {
                continue;
            }
            const float cx = static_cast<float>(MAP_WIDTH - 1 - col) * X + L * static_cast<float>(row) + halfW;
            const float cy = S * static_cast<float>(row) + T * static_cast<float>(col) + T + halfH;
            const float dx = worldX - cx;
            const float dy = py - cy;
            const float distSq = dx * dx + dy * dy;
            if (best < 0 || distSq < bestDistSq) {
                best = row * MAP_WIDTH + col;
                bestDistSq = distSq;
            }
        }
    }

    if (best < 0) {
        return std::nullopt;
    }
    return best;
}

/**
 * @brief Convert tile index to hex index for sprite operations
 *
 * Tiles use a 100x100 grid (10,000 positions) while hexes use a 200x200 grid (40,000 positions).
 * Each tile corresponds to a 2x2 block of hexes, so we multiply coordinates by 2.
 *
 * @param tileIndex Linear tile index (0-9999)
 * @return Corresponding hex index for the top-left hex of the tile (0-39999)
 */
inline int tileIndexToHexIndex(int tileIndex) {
    int tileX = tileIndex % MAP_WIDTH; // 0-99
    int tileY = tileIndex / MAP_WIDTH; // 0-99
    int hexX = tileX * 2;              // 0-198
    int hexY = tileY * 2;              // 0-198
    return hexY * HexagonGrid::GRID_WIDTH + hexX;
}

/**
 * @brief Convert hex index to tile index for data operations
 *
 * Converts from hex coordinates (200x200) to tile coordinates (100x100) by dividing by 2.
 * This gives the tile that contains the specified hex.
 *
 * @param hexIndex Linear hex index (0-39999)
 * @return Corresponding tile index (0-9999)
 */
inline int hexIndexToTileIndex(int hexIndex) {
    int hexX = hexIndex % HexagonGrid::GRID_WIDTH; // 0-199
    int hexY = hexIndex / HexagonGrid::GRID_WIDTH; // 0-199
    int tileX = hexX / 2;                          // 0-99
    int tileY = hexY / 2;                          // 0-99
    return tileY * MAP_WIDTH + tileX;
}

/**
 * @brief Common color constants for UI elements
 */
namespace TileColors {
    // Preview colors (semi-transparent yellow)
    inline sf::Color previewFill() {
        return sf::Color(Colors::PREVIEW_R, Colors::PREVIEW_G, Colors::PREVIEW_B, Colors::PREVIEW_ALPHA);
    }

    inline sf::Color previewOutline() {
        return sf::Color(Colors::PREVIEW_R, Colors::PREVIEW_G, Colors::PREVIEW_B, Colors::PREVIEW_OUTLINE_ALPHA);
    }

    // Error colors (semi-transparent red)
    inline sf::Color errorFill() {
        return sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, Colors::ERROR_ALPHA);
    }

    inline sf::Color errorOutline() {
        return sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, Colors::ERROR_OUTLINE_ALPHA);
    }

    // Selection rectangle colors
    inline sf::Color selectionFill() {
        return sf::Color(Colors::SELECTION_RECT_R, Colors::SELECTION_RECT_G,
            Colors::SELECTION_RECT_B, Colors::SELECTION_RECT_FILL_ALPHA);
    }

    inline sf::Color selectionOutline() {
        return sf::Color(Colors::SELECTION_RECT_R, Colors::SELECTION_RECT_G,
            Colors::SELECTION_RECT_B, Colors::SELECTION_RECT_OUTLINE_ALPHA);
    }

    // Standard colors
    inline sf::Color white() {
        return sf::Color::White;
    }

    // Transparent colors for empty tiles
    inline sf::Color transparent() {
        return sf::Color(255, 255, 255, 0);
    }

    // Exit grid highlight colors (bright magenta for better contrast against green)
    inline sf::Color exitGridHighlight() {
        return sf::Color(255, 0, 255, 180);
    }
}

/**
 * @brief Apply preview highlight to a sprite
 *
 * @param sprite The sprite to highlight
 */
inline void applyPreviewHighlight(sf::Sprite& sprite) {
    sprite.setColor(TileColors::previewFill());
}

/**
 * @brief Remove preview highlight from a sprite
 *
 * @param sprite The sprite to reset
 */
inline void removePreviewHighlight(sf::Sprite& sprite) {
    sprite.setColor(TileColors::white());
}

} // namespace geck

// Structured binding support for TileCoordinates
namespace std {
template <>
struct tuple_size<geck::TileCoordinates> : integral_constant<size_t, 2> { };
template <>
struct tuple_element<0, geck::TileCoordinates> {
    using type = unsigned int;
};
template <>
struct tuple_element<1, geck::TileCoordinates> {
    using type = unsigned int;
};
}

// Structured binding support for ScreenPosition
namespace std {
template <>
struct tuple_size<geck::ScreenPosition> : integral_constant<size_t, 2> { };
template <>
struct tuple_element<0, geck::ScreenPosition> {
    using type = unsigned int;
};
template <>
struct tuple_element<1, geck::ScreenPosition> {
    using type = unsigned int;
};
}
