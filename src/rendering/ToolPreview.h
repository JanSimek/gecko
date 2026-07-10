#pragma once

#include <SFML/Graphics/Sprite.hpp>

#include <memory>
#include <vector>

namespace geck {

class Object;

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
