#pragma once

#include <optional>
#include <vector>

#include <QPixmap>
#include <QString>

namespace sf {
class Sprite;
}

namespace geck {

/// Renders a set of positioned SFML sprites into a square thumbnail image. Generic and
/// content-agnostic — patterns and (later) maps build their own sprites and hand them
/// here, so the offscreen render + scale + in-memory cache live in one place.
class ThumbnailRenderer {
public:
    /// Look up a previously rendered thumbnail in the in-memory cache (Qt's QPixmapCache).
    /// Returns std::nullopt on a miss or an empty key.
    static std::optional<QPixmap> cached(const QString& key);

    /// Render `sprites` (already positioned, in back-to-front draw order) into a
    /// `size`x`size` transparent thumbnail, aspect-scaled and centered. When `cacheKey`
    /// is non-empty the result is stored in the cache. Returns a null QPixmap when there
    /// is nothing to draw.
    static QPixmap render(const std::vector<const sf::Sprite*>& sprites,
        int size,
        const QString& cacheKey = QString());
};

} // namespace geck
