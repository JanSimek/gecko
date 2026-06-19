#include "ObjectPicker.h"

#include <algorithm>
#include <iterator>
#include <spdlog/spdlog.h>

#include "editor/Object.h"
#include "rendering/ObjectVisibility.h"
#include "ui/core/EditorSession.h"

namespace geck {

std::vector<std::shared_ptr<Object>> ObjectPicker::objectsAtPosition(sf::Vector2f worldPos) const {
    std::vector<std::shared_ptr<Object>> objectsAtPos;

    // Only objects that are actually drawn are selectable: a click must never land on a
    // hidden object (e.g. a scroll blocker on a hidden layer) and produce an invisible
    // selection. isObjectVisible is the same rule RenderingEngine::renderObjects applies.
    std::ranges::copy_if(_session.objects(), std::back_inserter(objectsAtPos),
        [this, worldPos](const auto& object) {
            return isObjectVisible(object->getMapObject(), _session.visibility())
                && isPointInSpritePixel(worldPos, object->getSprite());
        });

    // Objects are drawn in _session.objects() order (see RenderingEngine::renderObjects), so the object
    // drawn last is the one visually on top. copy_if preserved that draw order, so reverse it to
    // put the topmost-drawn object first: the pick then matches exactly what the user sees, and
    // repeated clicks cycle stacked objects from top to bottom.
    std::ranges::reverse(objectsAtPos);

    if (objectsAtPos.size() > 1) {
        spdlog::debug("ObjectPicker: {} overlapping objects under cursor (topmost first)",
            objectsAtPos.size());
    }

    return objectsAtPos;
}

bool ObjectPicker::isSelectable(const std::shared_ptr<Object>& object) const {
    // Same rule objectsAtPosition applies for point picks; shared so area and point
    // selection agree on which objects are interactable.
    return object && isObjectVisible(object->getMapObject(), _session.visibility());
}

bool ObjectPicker::isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    return sprite.getGlobalBounds().contains(worldPos);
}

bool ObjectPicker::isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    if (!isPointInSpriteBounds(worldPos, sprite)) {
        return false;
    }

    const auto bounds = sprite.getGlobalBounds();
    const auto& texture = sprite.getTexture();

    // Convert world position to sprite-local, then to texture coordinates (accounting for texture rect and scale)
    sf::Vector2f localPos = worldPos - sf::Vector2f(bounds.position.x, bounds.position.y);
    const auto textureRect = sprite.getTextureRect();
    const auto scale = sprite.getScale();
    unsigned int texX = static_cast<unsigned int>((localPos.x / scale.x) + textureRect.position.x);
    unsigned int texY = static_cast<unsigned int>((localPos.y / scale.y) + textureRect.position.y);

    const auto texSize = texture.getSize();
    if (texX >= texSize.x || texY >= texSize.y) {
        return false;
    }

    const auto image = texture.copyToImage();
    const auto pixel = image.getPixel({ texX, texY });

    bool isHit = pixel.a > 0; // not fully transparent
    if (isHit) {
        spdlog::debug("Hit detected: world({:.2f},{:.2f}) -> local({:.2f},{:.2f}) -> tex({},{}) alpha={}",
            worldPos.x, worldPos.y, localPos.x, localPos.y, texX, texY, pixel.a);
    }

    return isHit;
}

} // namespace geck
