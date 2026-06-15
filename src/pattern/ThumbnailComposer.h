#pragma once

#include <memory>
#include <vector>

#include <SFML/Graphics/Sprite.hpp>

#include <QPixmap>
#include <QString>

namespace geck {
class Object;
} // namespace geck

namespace geck::pattern {

/// Flatten floor tiles, objects, then roof tiles into the editor's back-to-front draw
/// order and render them to a `size`x`size` thumbnail via the shared ThumbnailRenderer.
/// Shared by the pattern and map thumbnail compositors. `cacheKey` is forwarded to the
/// renderer's in-memory cache (an empty key disables caching).
///
/// Qt-dependent (returns a QPixmap), so it lives apart from the Qt-free sprite builders
/// in PatternSprite.h — keeping those usable from the headless editing library.
QPixmap composeThumbnail(const std::vector<sf::Sprite>& floorSprites,
    const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<sf::Sprite>& roofSprites,
    int size,
    const QString& cacheKey);

} // namespace geck::pattern
