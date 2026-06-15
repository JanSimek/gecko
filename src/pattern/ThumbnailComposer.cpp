#include "pattern/ThumbnailComposer.h"

#include "editor/Object.h"
#include "ui/rendering/ThumbnailRenderer.h"

namespace geck::pattern {

QPixmap composeThumbnail(const std::vector<sf::Sprite>& floorSprites,
    const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<sf::Sprite>& roofSprites, int size, const QString& cacheKey) {
    std::vector<const sf::Sprite*> ordered;
    ordered.reserve(floorSprites.size() + objects.size() + roofSprites.size());
    for (const sf::Sprite& sprite : floorSprites) {
        ordered.push_back(&sprite);
    }
    for (const auto& object : objects) {
        ordered.push_back(&object->getSprite());
    }
    for (const sf::Sprite& sprite : roofSprites) {
        ordered.push_back(&sprite);
    }
    return ThumbnailRenderer::render(ordered, size, cacheKey);
}

} // namespace geck::pattern
