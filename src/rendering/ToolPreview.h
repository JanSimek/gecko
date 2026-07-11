#pragma once

#include <SFML/Graphics/Sprite.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace geck {

class Object;

/// What a tool wants ghosted under the cursor, in engine terms (tile ids, PIDs, hex
/// positions) — never SFML draws. Tools return this from ITool::buildPreview(); the host
/// lowers it to sprites via pattern::buildTileSprite / pattern::buildSpriteObject with
/// the standard ghost alpha, so plugin tools (which cannot construct sprites) and native
/// tools share one preview channel and one art pipeline.
struct ToolPreviewSpec {
    struct Tile {
        int tileIndex = -1;
        uint16_t tileId = 0;
    };
    struct PlacedObject {
        uint32_t frmPid = 0;
        int hex = -1;
        uint32_t direction = 0;
    };

    std::vector<Tile> floorTiles;
    std::vector<PlacedObject> objects;
    std::vector<Tile> roofTiles;

    [[nodiscard]] bool empty() const {
        return floorTiles.empty() && objects.empty() && roofTiles.empty();
    }
};

/// The lowered, drawable form of a ToolPreviewSpec: the host resolves art and builds the
/// sprites; RenderingEngine just draws them (between the object layer and the roofs).
struct ToolPreview {
    std::vector<sf::Sprite> floorTiles;
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<sf::Sprite> roofTiles;

    [[nodiscard]] bool empty() const {
        return floorTiles.empty() && objects.empty() && roofTiles.empty();
    }

    void clear() {
        floorTiles.clear();
        objects.clear();
        roofTiles.clear();
    }
};

} // namespace geck
