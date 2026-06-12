#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <SFML/Graphics/Sprite.hpp>

#include <QPixmap>
#include <QString>

namespace geck {
class HexagonGrid;
class Object;
namespace resource {
    class GameResources;
}
} // namespace geck

namespace geck::pattern {

/// Resolve `frmPid` to game art and build an Object sprite positioned at hex `hex`,
/// facing `direction`. Returns nullptr if the art cannot be resolved/loaded (the
/// caller skips it). Shared by the stamper, the stamp ghost preview and the
/// thumbnail compositor so the FRM->sprite->Object path lives in one place.
std::shared_ptr<Object> buildSpriteObject(resource::GameResources& resources,
    const HexagonGrid& hexgrid,
    uint32_t frmPid,
    int hex,
    uint32_t direction);

/// Build a floor/roof tile sprite (tile id -> tiles.lst name -> art) positioned at the
/// tile's isometric screen coordinates. Returns std::nullopt for empty/unresolvable
/// tiles. Shared by the thumbnail compositor and the stamp ghost preview.
std::optional<sf::Sprite> buildTileSprite(resource::GameResources& resources,
    int tileIndex,
    bool isRoof,
    uint16_t tileId);

/// Flatten floor tiles, objects, then roof tiles into the editor's back-to-front draw
/// order and render them to a `size`x`size` thumbnail via the shared ThumbnailRenderer.
/// Shared by the pattern and map thumbnail compositors. `cacheKey` is forwarded to the
/// renderer's in-memory cache (an empty key disables caching).
QPixmap composeThumbnail(const std::vector<sf::Sprite>& floorSprites,
    const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<sf::Sprite>& roofSprites,
    int size,
    const QString& cacheKey);

} // namespace geck::pattern
