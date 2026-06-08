#pragma once

#include "Constants.h"
#include <SFML/Graphics/Color.hpp>

namespace geck::ColorUtils {

// SFML Color utilities
inline sf::Color createSelectionFillColor() {
    return sf::Color(Colors::SELECTION_RECT_R, Colors::SELECTION_RECT_G, Colors::SELECTION_RECT_B, Colors::SELECTION_RECT_FILL_ALPHA);
}

inline sf::Color createSelectionOutlineColor() {
    return sf::Color(Colors::SELECTION_RECT_R, Colors::SELECTION_RECT_G, Colors::SELECTION_RECT_B, Colors::SELECTION_RECT_OUTLINE_ALPHA);
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

inline sf::Color createRoofTileSelectionColor() {
    return sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, Colors::ROOF_SELECTION_ALPHA);
}

inline sf::Color createObjectSelectionColor() {
    return sf::Color::Magenta;
}

} // namespace geck::ColorUtils