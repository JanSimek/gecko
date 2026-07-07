#include "Object.h"
#include "editor/HexagonGrid.h"
#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "format/map/MapObject.h"
#include "util/Constants.h"
#include "util/Coordinates.h"
#include "util/Exceptions.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace geck {

Object::Object(const Frm* frm)
    : _sprite(createBlankTexture())
    , _frm(frm)
    , _direction(0)
    , _selected(false) {

    if (!frm) {
        spdlog::error("Object constructor: FRM pointer is null");
        throw SpriteException("Cannot create Object with null FRM");
    }

    if (frm->directions().empty()) {
        spdlog::error("Object constructor: FRM '{}' has no directions (size: {})",
            frm->filename(), frm->directions().size());
        throw SpriteException("FRM has no directions", frm->filename());
    }

    if (frm->directions().at(0).frames().empty()) {
        spdlog::error("Object constructor: FRM '{}' direction 0 has no frames (size: {})",
            frm->filename(), frm->directions().at(0).frames().size());
        throw SpriteException("FRM direction has no frames", frm->filename());
    }
}

sf::Texture& Object::createBlankTexture() {
    // Intentionally leaked: this tiny 1x1 texture must outlive the OpenGL context.
    // Static sf::Texture objects are destroyed during __cxa_finalize after the GL
    // context is gone, causing a crash in sf::Texture::~Texture().  Heap-allocating
    // without delete avoids the static destruction order problem entirely.
    static sf::Texture* texture = [] {
        auto* t = new sf::Texture();
        sf::Image blankImage{ sf::Vector2u{ 1, 1 }, sf::Color::Transparent };
        [[maybe_unused]] const bool loadSuccess = t->loadFromImage(blankImage);
        return t;
    }();
    return *texture;
}

MapObject& Object::getMapObject() {
    if (!_mapObject) {
        throw ObjectException("getMapObject() called but _mapObject is null");
    }
    return *_mapObject;
}

std::shared_ptr<MapObject> Object::getMapObjectPtr() const noexcept {
    return _mapObject;
}

bool Object::hasMapObject() const noexcept {
    return _mapObject != nullptr;
}

void Object::setMapObject(std::shared_ptr<MapObject> newMapObject) {
    _mapObject = newMapObject;
}

void Object::setSprite(sf::Sprite sprite) {
    // Selection is drawn as an outline keyed off _selected, so a swapped-in sprite needs no colour fix-up.
    _sprite = std::move(sprite);
}

const sf::Sprite& Object::getSprite() const noexcept {
    return _sprite;
}

sf::Sprite& Object::getSprite() noexcept {
    return _sprite;
}

void Object::setFrm(const Frm* frm) {
    if (!frm) {
        spdlog::error("Object::setFrm - FRM pointer is null");
        return;
    }

    if (frm->directions().empty()) {
        spdlog::error("Object::setFrm - FRM '{}' has no directions", frm->filename());
        return;
    }

    if (frm->directions()[0].frames().empty()) {
        spdlog::error("Object::setFrm - FRM '{}' direction 0 has no frames", frm->filename());
        return;
    }

    _frm = frm;

    _direction = 0;
    setDirection(ObjectDirection(_direction));
}

void Object::setHexPosition(const Hex& hex) {

    // center on the hex
    WorldCoords position(
        static_cast<float>(hex.x() - (width() / 2) + shiftX()),
        static_cast<float>(hex.y() - height() + shiftY()));

    _sprite.setPosition(position.toVector());
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

void Object::setDirection(ObjectDirection direction) {
    _direction = static_cast<int>(direction);

    // Clamp out-of-range direction index. arcaves.map has one scrblk object with an
    // invalid direction that triggers this.
    if (_frm->directions().size() <= static_cast<size_t>(_direction) || _direction < 0) {
        spdlog::error("Object has orientation index {} but the FRM has only [{}] orientations", _direction, _frm->directions().size());
        _direction = 0;
    }

    if (_mapObject != nullptr) {
        _mapObject->direction = _direction;
    }

    auto first_frame = _frm->directions().at(_direction).frames().at(0);

    uint16_t left = 0;
    uint16_t top = static_cast<uint16_t>(_direction * _frm->maxFrameHeight());
    uint16_t width = first_frame.width();
    uint16_t height = first_frame.height();

    _sprite.setTextureRect({ { left, top }, { width, height } });
}

void Object::rotate() {
    if (static_cast<size_t>(_direction + 1) >= _frm->directions().size()) {
        _direction = 0;
    } else {
        _direction++;
    }
    setDirection(ObjectDirection(_direction));
}

void Object::select() {
    // Selection is shown as an outline drawn by the renderer (RenderingEngine::drawObjectOutline),
    // so the sprite keeps its real colours — only the flag is toggled here.
    _selected = true;
}

void Object::unselect() {
    _selected = false;
}

bool Object::isSelected() const noexcept {
    return _selected;
}

bool Object::hasLight() const noexcept {
    return _mapObject && _mapObject->isLightSource();
}

} // namespace geck
