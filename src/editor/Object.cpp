#include "Object.h"
#include "editor/HexagonGrid.h"
#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "format/map/MapObject.h"
#include "util/Constants.h"
#include "util/Coordinates.h"
#include "util/Exceptions.h"
#include "util/ExitGridDirection.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace geck {

Object::Object(const Frm* frm)
    : _sprite(createBlankTexture())
    , _frm(frm)
    , _direction(0)
    , _selected(false)
    , _showLightOverlay(false) {

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

    initializeLightOverlay();
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

    // EDITOR-DISPLAY ONLY: push an exit-grid marker's bar OUTWARD so the trigger hex sits at the bar's
    // INNER edge (the default bottom-anchor straddles the hex, reading ambiguously). The Fallout 2
    // engine applies NO such offset — this is a deliberate authoring-clarity divergence, exit grids only.
    applyExitGridOutwardOffset(hex);

    if (_showLightOverlay && hasLight()) {
        updateLightOverlay();
    }
}

int Object::exitGridDirection() const {
    // Placed markers carry a MapObject whose proto index (16..23) is the direction.
    if (_mapObject != nullptr && _mapObject->isExitGridMarker()) {
        return static_cast<int>(_mapObject->protoId()) - static_cast<int>(MapObject::EXIT_GRID_PID_INDEX_FIRST);
    }
    // Preview / bare-FRM objects (no MapObject) are identified by art name: exitgrd1..8 / ext2grd1..8.
    const std::string& fn = _frm != nullptr ? _frm->filename() : std::string();
    if (fn.size() >= 8 && (fn.rfind("exitgrd", 0) == 0 || fn.rfind("ext2grd", 0) == 0)) {
        const int dir = fn[7] - '1';
        if (dir >= 0 && dir < ExitGrid::DIR_COUNT) {
            return dir;
        }
    }
    return -1;
}

void Object::applyExitGridOutwardOffset(const Hex& hex) {
    const int dir = exitGridDirection();
    if (dir < 0) {
        return;
    }
    const auto [outX, outY] = exitGridOutward(dir);
    if (outX == 0 && outY == 0) {
        return;
    }

    // A DIAGONAL marker (both components nonzero) gets a MODERATE slide PERPENDICULAR to the band, not
    // the bbox-corner slide the cardinals use: the bars are large (127x48, 111x60), so a bbox corner
    // would combine half-width AND half-height (~93px) and shove the bar clear off its hex.
    // exitGridOutward gives the perpendicular-to-band normal; normalize it, then slide a fixed distance.
    // A flip (dir ^ 1) reverses the sign, swinging the byte-identical art to the OTHER side.
    if (outX != 0 && outY != 0) {
        // Seat the ORIGINAL row so its HEX-SIDE (inner) edge lands ON the trigger hex: slide OUTWARD by
        // half the rendered band's perpendicular thickness (~37px row / 2). The renderer then stacks the
        // SECOND row further outward, so the whole doubled band lies on one side of the hex line with the
        // hex at its very edge. FLIPPABLE: a flip (dir ^ 1) reverses this vector and the second-row
        // offset together, swinging the whole band to the other side, hex still at the edge.
        constexpr float kDiagonalSlide = 18.5f;
        const float len = std::sqrt(static_cast<float>(outX * outX + outY * outY));
        _sprite.move(sf::Vector2f(
            static_cast<float>(outX) / len * kDiagonalSlide,
            static_cast<float>(outY) / len * kDiagonalSlide));
        return;
    }

    // CARDINAL marker: slide along the outward axis until the trigger hex lands on the bar's inner edge
    // (the bbox side opposite the outward direction). Reading the actual on-screen bounds adapts to both
    // thin pieces (96x24, 32x96) and both art families without per-piece pixel constants.
    const sf::FloatRect bounds = _sprite.getGlobalBounds();
    const float left = bounds.position.x;
    const float top = bounds.position.y;
    const float right = left + bounds.size.x;
    const float bottom = top + bounds.size.y;

    float shiftX = 0.0f;
    if (outX < 0) { // extends left -> inner edge is the right side
        shiftX = static_cast<float>(hex.x()) - right;
    } else if (outX > 0) { // extends right -> inner edge is the left side
        shiftX = static_cast<float>(hex.x()) - left;
    }
    float shiftY = 0.0f;
    if (outY < 0) { // extends up -> inner edge is the bottom side
        shiftY = static_cast<float>(hex.y()) - bottom;
    } else if (outY > 0) { // extends down -> inner edge is the top side
        shiftY = static_cast<float>(hex.y()) - top;
    }

    _sprite.move(sf::Vector2f(shiftX, shiftY));
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

void Object::initializeLightOverlay() {
    _lightOverlay.setFillColor(sf::Color(255, 255, 100, 30));    // Light yellow, semi-transparent
    _lightOverlay.setOutlineColor(sf::Color(255, 255, 150, 80)); // Brighter yellow outline
    _lightOverlay.setOutlineThickness(1.0f);
    _lightOverlay.setPointCount(6); // Hexagonal shape to match the grid
}

void Object::setShowLightOverlay(bool show) {
    _showLightOverlay = show;
    if (show && _mapObject) {
        updateLightOverlay();
    }
}

void Object::updateLightOverlay() {
    if (!_mapObject || !_mapObject->isLightSource()) {
        return;
    }

    // Each hex is approximately 32 pixels wide in standard zoom.
    float hexWidth = 32.0f;
    float radius = _mapObject->light_radius * hexWidth / 2.0f;

    _lightOverlay.setRadius(radius);
    _lightOverlay.setOrigin(sf::Vector2f(radius, radius));

    // Position the overlay at the object's center.
    auto spritePos = _sprite.getPosition();
    auto spriteBounds = _sprite.getLocalBounds();
    _lightOverlay.setPosition(sf::Vector2f(
        spritePos.x + spriteBounds.size.x / 2.0f,
        spritePos.y + spriteBounds.size.y / 2.0f));

    auto color = _lightOverlay.getFillColor();
    // Map light intensity (engine range 0-65535) to alpha (30-100).
    uint8_t alpha = static_cast<uint8_t>(30 + (_mapObject->light_intensity / 65535.0f) * 70);
    color.a = alpha;
    _lightOverlay.setFillColor(color);
}

bool Object::hasLight() const noexcept {
    return _mapObject && _mapObject->isLightSource();
}

} // namespace geck
