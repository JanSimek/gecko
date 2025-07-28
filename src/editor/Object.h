#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include <SFML/Graphics.hpp>

#include "editor/Hex.h"

namespace geck {

/**
 * @brief Object facing directions in the Fallout 2 hex grid system
 */
enum class ObjectDirection : int {
    NORTH_EAST = 0, ///< Facing North-East (default)
    EAST = 1,       ///< Facing East
    SOUTH_EAST = 2, ///< Facing South-East
    SOUTH_WEST = 3, ///< Facing South-West
    WEST = 4,       ///< Facing West
    NORTH_WEST = 5  ///< Facing North-West
};

struct MapObject;
class Frm;

class Object {
private:
    sf::Sprite _sprite;
    sf::CircleShape _lightOverlay; // Light radius visualization

    std::shared_ptr<MapObject> _mapObject;

    const Frm* _frm;
    int _direction;
    bool _selected;
    bool _showLightOverlay;

public:
    Object(const Frm* frm);

    [[nodiscard]] MapObject& getMapObject();
    [[nodiscard]] bool hasMapObject() const noexcept;
    void setMapObject(std::shared_ptr<MapObject> newMapObject);

    void setSprite(sf::Sprite sprite);
    [[nodiscard]] const sf::Sprite& getSprite() const noexcept;
    [[nodiscard]] sf::Sprite& getSprite() noexcept;

    void setFrm(const Frm* frm);
    [[nodiscard]] const Frm* getFrm() const noexcept { return _frm; }

    void setHexPosition(const Hex& hex);
    void setDirection(ObjectDirection direction);
    void rotate();

    void select();
    void unselect();
    [[nodiscard]] bool isSelected() const noexcept;
    
    // Light overlay methods
    void setShowLightOverlay(bool show);
    [[nodiscard]] bool isShowingLightOverlay() const noexcept { return _showLightOverlay; }
    void updateLightOverlay();
    [[nodiscard]] const sf::CircleShape& getLightOverlay() const noexcept { return _lightOverlay; }
    [[nodiscard]] bool hasLight() const noexcept;

    [[nodiscard]] int16_t shiftX() const;
    [[nodiscard]] int16_t shiftY() const;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

private:
    static sf::Texture& createBlankTexture();
    void initializeLightOverlay();
};

} // namespace geck
