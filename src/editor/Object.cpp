#include "Object.h"
#include "editor/HexagonGrid.h"
#include "format/map/MapObject.h"
#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "../util/ResourceManager.h"

#include <spdlog/spdlog.h>

namespace geck {

Object::Object(const Frm* frm)
    : _frm(frm)
    , _direction(0)
    , _selected(false) {
}

MapObject& Object::getMapObject() {
    return *_mapObject; //.get();
}

void Object::setMapObject(std::shared_ptr<MapObject> newMapObject) {
    _mapObject = newMapObject;
}

void Object::setSprite(sf::Sprite sprite) {
    _sprite = sprite;
}

const sf::Sprite& Object::getSprite() const {
    return _sprite;
}

void Object::setHexPosition(const Hex& hex) {

    // center on the hex
    float x = static_cast<float>(hex.x() - (width() / 2) + shiftX());
    float y = static_cast<float>(hex.y() - height() + shiftY());

    _sprite.setPosition(x, y);
    if (_mapObject != nullptr) {
        _mapObject->position = hex.position();
    }
}

int16_t Object::shiftX() const {
    return _frm->directions().at(_direction).shiftX();
}

int16_t Object::shiftY() const {
    return _frm->directions().at(_direction).shiftY();
}

int Object::width() const {
    return _frm->directions().at(_direction).frames().at(0).width();
}

int Object::height() const {
    return _frm->directions().at(_direction).frames().at(0).height();
}

void Object::setDirection(int direction) {
    _direction = direction;

    // int hexPos = object->hexPosition();
    // float x = hexPos % 200;
    // float y = hexPos / 200;
    // Point point = hexToScreen(x, y);

    // FIXME: ??? one scrblk on arcaves.map
    if (_frm->directions().size() <= static_cast<size_t>(_direction) || _direction < 0) {
        spdlog::error("Object has orientation index {} but the FRM has only [{}] orientations", _direction, _frm->directions().size());
        _direction = 0;
    }

    _mapObject->direction = _direction;

    // Take the first frame of the direction
    auto first_frame = _frm->directions().at(_direction).frames().at(0);

    uint16_t left = 0;
    uint16_t top = _direction * _frm->maxFrameHeight();
    uint16_t width = first_frame.width();
    uint16_t height = first_frame.height();

    _sprite.setTextureRect({ left, top, width, height });
}

void Object::rotate() {
    if (static_cast<size_t>(_direction + 1) >= _frm->directions().size()) {
        _direction = 0;
    } else {
        _direction++;
    }
    setDirection(_direction);
}

void Object::select() {
    _sprite.setColor(sf::Color::Magenta);
    _selected = true;
}

void Object::unselect() {
    _sprite.setColor(sf::Color::White);
    _selected = false;
}

bool Object::isSelected() {
    return _selected;
}

} // namespace geck
