#pragma once

#include <SFML/Graphics.hpp>
#include <vector>

namespace geck {

class Hex;
class HexagonGrid;
namespace resource {
    class GameResources;
}

class HexRenderer {
public:
    explicit HexRenderer(resource::GameResources& resources);

    void renderGrid(sf::RenderTarget& target,
        const sf::View& view,
        const HexagonGrid& hexGrid,
        int currentHoverHex) const;

    void renderSelection(sf::RenderTarget& target,
        const HexagonGrid& hexGrid,
        const std::vector<int>& selectedHexPositions) const;

    void renderHighlights(sf::RenderTarget& target,
        const HexagonGrid& hexGrid,
        int currentHoverHex,
        int playerPositionHex) const;

private:
    [[nodiscard]] const sf::Texture& hexTexture() const;
    static sf::IntRect overlayTextureRect(const sf::Texture& texture);

    void drawGridSprite(sf::RenderTarget& target, const Hex& hex) const;
    void drawOverlaySprite(sf::RenderTarget& target,
        const Hex& hex,
        const sf::Sprite& spriteTemplate) const;
    bool isHexVisible(const Hex& hex, const sf::View& view) const;

    resource::GameResources& _resources;
    sf::Sprite _gridSprite;
    sf::Sprite _hoverSprite;
    sf::Sprite _selectionSprite;
    sf::Sprite _playerPositionSprite;
};

} // namespace geck
