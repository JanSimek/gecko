#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include <SFML/Graphics.hpp>

#include "editor/Hex.h"

namespace geck {

/**
 * @brief Object facing directions in the Fallout 2 hex grid system
 */
enum class ObjectDirection : int {
    NORTH_EAST = 0,   ///< Facing North-East (default)
    EAST = 1,         ///< Facing East
    SOUTH_EAST = 2,   ///< Facing South-East
    SOUTH_WEST = 3,   ///< Facing South-West  
    WEST = 4,         ///< Facing West
    NORTH_WEST = 5    ///< Facing North-West
};

struct MapObject;
class Frm;

class Object {
private:
    sf::Sprite _sprite;

    std::shared_ptr<MapObject> _mapObject;

    const Frm* _frm;
    int _direction;
    bool _selected;

public:
    Object(const Frm* frm);

    MapObject& getMapObject();
    void setMapObject(std::shared_ptr<MapObject> newMapObject);

    void setSprite(sf::Sprite sprite);
    const sf::Sprite& getSprite() const;
    sf::Sprite& getSprite();

    void setHexPosition(const Hex& hex);
    void setDirection(ObjectDirection direction);
    void rotate();

    void select();
    void unselect();
    bool isSelected();

    // sf::RectangleShape border -> selected

    int16_t shiftX() const;
    int16_t shiftY() const;

    int width() const;
    int height() const;
};

} // namespace geck
