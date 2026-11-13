#include "HexRenderer.h"
#include "editor/Hex.h"
#include "editor/HexagonGrid.h"
#include "resource/GameResources.h"
#include "viewport/ViewportController.h"
#include "util/Constants.h"
#include "resource/ResourcePaths.h"

namespace geck {

namespace {

    constexpr int HEX_OVERLAY_FRAME_COUNT = 2;

}

HexRenderer::HexRenderer(resource::GameResources& resources)
    : _resources(resources)
    , _gridSprite(hexTexture())
    , _hoverSprite(hexTexture())
    , _selectionSprite(hexTexture())
    , _playerPositionSprite(hexTexture()) {
    const sf::IntRect overlayRect = overlayTextureRect(_gridSprite.getTexture());

    _hoverSprite.setTextureRect(overlayRect);
    _hoverSprite.setColor(sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, Colors::FULLY_OPAQUE));

    _selectionSprite.setTextureRect(overlayRect);
    _selectionSprite.setColor(sf::Color(Colors::SELECTION_R, Colors::SELECTION_G, Colors::SELECTION_B, SelectionColors::HEX_ALPHA));

    _playerPositionSprite.setTextureRect(overlayRect);
    _playerPositionSprite.setColor(sf::Color(PlayerColors::POSITION_R, PlayerColors::POSITION_G, PlayerColors::POSITION_B, PlayerColors::POSITION_ALPHA));
}

void HexRenderer::renderGrid(sf::RenderTarget& target,
    const sf::View& view,
    const HexagonGrid& hexGrid,
    int currentHoverHex) const {
    for (const auto& hex : hexGrid.grid()) {
        if (static_cast<int>(hex.position()) == currentHoverHex || !ViewportController::isHexVisible(hex.x(), hex.y(), view)) {
            continue;
        }

        drawGridSprite(target, hex);
    }
}

void HexRenderer::renderSelection(sf::RenderTarget& target,
    const HexagonGrid& hexGrid,
    const std::vector<int>& selectedHexPositions) const {
    for (int hexPosition : selectedHexPositions) {
        auto hex = hexGrid.getHexByPosition(static_cast<uint32_t>(hexPosition));
        if (!hex.has_value()) {
            continue;
        }

        drawOverlaySprite(target, hex->get(), _selectionSprite);
    }
}

void HexRenderer::renderHighlights(sf::RenderTarget& target,
    const HexagonGrid& hexGrid,
    int currentHoverHex,
    int playerPositionHex) const {
    if (currentHoverHex >= 0) {
        auto hoverHex = hexGrid.getHexByPosition(static_cast<uint32_t>(currentHoverHex));
        if (hoverHex.has_value()) {
            drawOverlaySprite(target, hoverHex->get(), _hoverSprite);
        }
    }

    if (playerPositionHex >= 0) {
        auto playerHex = hexGrid.getHexByPosition(static_cast<uint32_t>(playerPositionHex));
        if (playerHex.has_value()) {
            drawOverlaySprite(target, playerHex->get(), _playerPositionSprite);
        }
    }
}

void HexRenderer::renderHexOverlay(sf::RenderTarget& target,
    const sf::View& view,
    const HexagonGrid& hexGrid,
    const std::vector<int>& hexPositions,
    sf::Color color) const {
    sf::Sprite overlay = _gridSprite;
    overlay.setTextureRect(overlayTextureRect(_gridSprite.getTexture()));
    overlay.setColor(color);
    for (int hexPosition : hexPositions) {
        auto hex = hexGrid.getHexByPosition(static_cast<uint32_t>(hexPosition));
        if (!hex.has_value() || !ViewportController::isHexVisible(hex->get().x(), hex->get().y(), view)) {
            continue;
        }
        drawOverlaySprite(target, hex->get(), overlay);
    }
}

const sf::Texture& HexRenderer::hexTexture() const {
    return _resources.textures().get(ResourcePaths::Frm::HEX_GRID);
}

sf::IntRect HexRenderer::overlayTextureRect(const sf::Texture& texture) {
    const sf::Vector2u textureSize = texture.getSize();
    const int overlayWidth = static_cast<int>(textureSize.x / HEX_OVERLAY_FRAME_COUNT);

    return sf::IntRect(
        sf::Vector2i(overlayWidth, 0),
        sf::Vector2i(overlayWidth, static_cast<int>(textureSize.y)));
}

void HexRenderer::drawGridSprite(sf::RenderTarget& target, const Hex& hex) const {
    sf::Sprite hexSprite = _gridSprite;
    hexSprite.setPosition({ static_cast<float>(hex.x() - Hex::HEX_WIDTH),
        static_cast<float>(hex.y() - Hex::HEX_HEIGHT + Hex::SPRITE_BASELINE_OFFSET) });
    target.draw(hexSprite);
}

void HexRenderer::drawOverlaySprite(sf::RenderTarget& target,
    const Hex& hex,
    const sf::Sprite& spriteTemplate) const {
    sf::Sprite overlaySprite = spriteTemplate;
    overlaySprite.setPosition({ static_cast<float>(hex.x() + SpriteOffset::HEX_HIGHLIGHT_X),
        static_cast<float>(hex.y() + SpriteOffset::HEX_HIGHLIGHT_Y) });
    target.draw(overlaySprite);
}

} // namespace geck
