#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SFML/Graphics.hpp>

#include "editor/Hex.h"
#include "format/ObjTypes.h"

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

    [[nodiscard]] MapObject& getMapObject();
    [[nodiscard]] bool hasMapObject() const noexcept;
    [[nodiscard]] std::shared_ptr<MapObject> getMapObjectPtr() const noexcept;
    void setMapObject(std::shared_ptr<MapObject> newMapObject);

    void setSprite(sf::Sprite sprite);
    [[nodiscard]] const sf::Sprite& getSprite() const noexcept;
    [[nodiscard]] sf::Sprite& getSprite() noexcept;

    [[nodiscard]] const Frm* getFrm() const noexcept { return _frm; }

    void setHexPosition(const Hex& hex);
    void setDirection(Rotation direction);
    [[nodiscard]] int getDirection() const noexcept { return _direction; }
    void rotate();

    void select();
    void unselect();
    [[nodiscard]] bool isSelected() const noexcept;

    // True when this object is a light source (its MapObject has a non-zero light radius or intensity).
    // The illuminated hexes themselves are drawn by RenderingEngine::renderLightOverlays.
    [[nodiscard]] bool hasLight() const noexcept;

    [[nodiscard]] int16_t shiftX() const;
    [[nodiscard]] int16_t shiftY() const;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    // A shared 1x1 transparent texture for sprites with nothing to draw yet (and empty tiles).
    static const sf::Texture& blankTexture();
};

} // namespace geck
