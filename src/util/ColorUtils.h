#pragma once

#include "Constants.h"
#include <SFML/Graphics/Color.hpp>

namespace geck::ColorUtils {

// Every selection visual shares the one theme accent (Colors::SELECTION_*) so selecting an
// object, a floor/roof tile, a hex or the marquee all read the same. The ERROR_* red is
// reserved for genuine errors/invalid states.

inline sf::Color createSelectionFillColor() {
    return sf::Color(Colors::SELECTION_R, Colors::SELECTION_G, Colors::SELECTION_B, Colors::SELECTION_RECT_FILL_ALPHA);
}

inline sf::Color createSelectionOutlineColor() {
    return sf::Color(Colors::SELECTION_R, Colors::SELECTION_G, Colors::SELECTION_B, Colors::SELECTION_RECT_OUTLINE_ALPHA);
}

// Tint applied to a selected object's sprite (multiplied, so full alpha).
inline sf::Color createObjectSelectionColor() {
    return sf::Color(Colors::SELECTION_R, Colors::SELECTION_G, Colors::SELECTION_B);
}

inline sf::Color createFloorTileSelectionColor() {
    return sf::Color(Colors::SELECTION_R, Colors::SELECTION_G, Colors::SELECTION_B, Colors::SELECTION_TILE_ALPHA);
}

inline sf::Color createRoofTileSelectionColor() {
    return sf::Color(Colors::SELECTION_R, Colors::SELECTION_G, Colors::SELECTION_B, Colors::SELECTION_TILE_ALPHA);
}

inline sf::Color createPreviewHighlightColor() {
    return sf::Color(Colors::PREVIEW_R, Colors::PREVIEW_G, Colors::PREVIEW_B, Colors::PREVIEW_ALPHA);
}

inline sf::Color createPreviewOutlineColor() {
    return sf::Color(Colors::PREVIEW_R, Colors::PREVIEW_G, Colors::PREVIEW_B, Colors::PREVIEW_OUTLINE_ALPHA);
}

inline sf::Color createErrorIndicatorColor() {
    return sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, Colors::ERROR_ALPHA);
}

inline sf::Color createErrorOutlineColor() {
    return sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, Colors::ERROR_OUTLINE_ALPHA);
}

} // namespace geck::ColorUtils
