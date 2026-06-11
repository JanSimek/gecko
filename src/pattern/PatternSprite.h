#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <SFML/Graphics/Sprite.hpp>

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

} // namespace geck::pattern
