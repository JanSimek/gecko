#include "pattern/ThumbnailComposer.h"

#include "editor/Object.h"
#include "ui/rendering/ThumbnailRenderer.h"

namespace geck::pattern {

namespace {

    std::vector<const sf::Sprite*> drawOrder(const std::vector<sf::Sprite>& floorSprites,
        const std::vector<std::shared_ptr<Object>>& objects,
        const std::vector<sf::Sprite>& roofSprites) {
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
        return ordered;
    }

} // namespace

QPixmap composeThumbnail(const std::vector<sf::Sprite>& floorSprites,
    const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<sf::Sprite>& roofSprites, int size, const QString& cacheKey) {
    return ThumbnailRenderer::render(drawOrder(floorSprites, objects, roofSprites), size, cacheKey);
}

QImage composeThumbnailImage(const std::vector<sf::Sprite>& floorSprites,
    const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<sf::Sprite>& roofSprites, int size) {
    return ThumbnailRenderer::renderImage(drawOrder(floorSprites, objects, roofSprites), size);
}

} // namespace geck::pattern
