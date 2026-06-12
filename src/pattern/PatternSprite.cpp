#include "pattern/PatternSprite.h"

#include <string>

#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/frm/Frm.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "resource/GameResources.h"
#include "util/TileUtils.h"

namespace geck::pattern {

std::shared_ptr<Object> buildSpriteObject(resource::GameResources& resources,
    const HexagonGrid& hexgrid, uint32_t frmPid, int hex, uint32_t direction) {
    const Frm* frm = nullptr;
    std::string frmPath;
    try {
        frmPath = resources.frmResolver().resolve(frmPid);
        if (!frmPath.empty()) {
            frm = resources.repository().load<Frm>(frmPath);
        }
    } catch (const std::exception& e) {
        spdlog::warn("buildSpriteObject: FRM resolve/load failed for fid {}: {}", frmPid, e.what());
    }

    // Object requires valid art (it draws a sprite); without a resolvable FRM there is
    // nothing to build, so the caller skips this entry.
    if (frm == nullptr) {
        return nullptr;
    }

    try {
        auto object = std::make_shared<Object>(frm);
        sf::Sprite sprite{ resources.textures().get(frmPath) };
        object->setSprite(std::move(sprite));
        object->setDirection(static_cast<ObjectDirection>(direction));
        if (auto h = hexgrid.getHexByPosition(static_cast<uint32_t>(hex)); h.has_value()) {
            object->setHexPosition(h->get());
        }
        return object;
    } catch (const std::exception& e) {
        spdlog::warn("buildSpriteObject: failed to build object for fid {}: {}", frmPid, e.what());
        return nullptr;
    }
}

std::optional<sf::Sprite> buildTileSprite(resource::GameResources& resources,
    int tileIndex, bool isRoof, uint16_t tileId) {
    if (tileId == static_cast<uint16_t>(Map::EMPTY_TILE)) {
        return std::nullopt;
    }
    const auto* tileList = resources.repository().find<Lst>("art/tiles/tiles.lst");
    if (tileList == nullptr || tileId >= tileList->list().size()) {
        return std::nullopt;
    }
    try {
        const std::string tilePath = "art/tiles/" + tileList->list()[tileId];
        sf::Sprite sprite{ resources.textures().get(tilePath) };
        const ScreenPosition pos = indexToScreenPosition(tileIndex, isRoof);
        sprite.setPosition({ static_cast<float>(pos.x), static_cast<float>(pos.y) });
        return sprite;
    } catch (const std::exception& e) {
        spdlog::warn("buildTileSprite: tile {} art failed: {}", tileId, e.what());
        return std::nullopt;
    }
}

} // namespace geck::pattern
