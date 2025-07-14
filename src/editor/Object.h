#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include <SFML/Graphics.hpp>

#include "editor/Hex.h"

namespace geck {

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
    void setDirection(int direction); // TODO: enum
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
