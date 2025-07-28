#pragma once

#include <SFML/Graphics.hpp>
#include "Constants.h"

namespace geck {

/**
 * @brief Utility functions for tile position calculations and common operations
 *
 * This file contains refactored common tile operations to reduce code duplication
 * and improve maintainability.
 */

/**
 * @brief Represents tile coordinates in the grid
 */
struct TileCoordinates {
    unsigned int x; ///< X coordinate (row) in the tile grid
    unsigned int y; ///< Y coordinate (column) in the tile grid

    TileCoordinates(unsigned int initX = 0, unsigned int initY = 0)
        : x(initX)
        , y(initY) { }
};

/**
 * @brief Represents screen position in pixels
 */
struct ScreenPosition {
    unsigned int x; ///< X position in pixels
    unsigned int y; ///< Y position in pixels

    ScreenPosition(unsigned int posX = 0, unsigned int posY = 0)
        : x(posX)
        , y(posY) { }
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
        // Apply roof offset
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