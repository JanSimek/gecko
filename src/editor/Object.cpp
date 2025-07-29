#include "Object.h"
#include "editor/HexagonGrid.h"
#include "format/map/MapObject.h"
#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "../util/ResourceManager.h"
#include "../util/Constants.h"
#include "../util/ColorUtils.h"
#include "../util/Exceptions.h"
#include "../util/Coordinates.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace geck {

Object::Object(const Frm* frm)
    : _sprite(createBlankTexture())
    , _frm(frm)
    , _direction(0)
    , _selected(false)
    , _showLightOverlay(false) {
    
    // Validate FRM has at least one direction
    if (!frm) {
        spdlog::error("Object constructor: FRM pointer is null");
        throw SpriteException("Cannot create Object with null FRM");
    }
    
    if (frm->directions().empty()) {
        spdlog::error("Object constructor: FRM '{}' has no directions (size: {})", 
                     frm->filename(), frm->directions().size());
        throw SpriteException("FRM has no directions", frm->filename());
    }
    
    // Validate first direction has at least one frame
    if (frm->directions().at(0).frames().empty()) {
        spdlog::error("Object constructor: FRM '{}' direction 0 has no frames (size: {})", 
                     frm->filename(), frm->directions().at(0).frames().size());
        throw SpriteException("FRM direction has no frames", frm->filename());
    }

    // Initialize light overlay
    initializeLightOverlay();
}

sf::Texture& Object::createBlankTexture() {
    static const std::string BLANK_TEXTURE_KEY = "__object_blank_texture__";

    auto& resourceManager = ResourceManager::getInstance();

    // Check if texture already exists in ResourceManager
    try {
        return const_cast<sf::Texture&>(resourceManager.texture(BLANK_TEXTURE_KEY));
    } catch (const std::exception&) {
        // Texture doesn't exist, create it
        sf::Image blankImage{ sf::Vector2u{ 1, 1 }, sf::Color::Transparent };
        auto texture = std::make_unique<sf::Texture>();
        [[maybe_unused]] bool loadSuccess = texture->loadFromImage(blankImage);
        resourceManager.storeTexture(BLANK_TEXTURE_KEY, std::move(texture));
        return const_cast<sf::Texture&>(resourceManager.texture(BLANK_TEXTURE_KEY));
    }
}

MapObject& Object::getMapObject() {
    if (!_mapObject) {
        throw ObjectException("getMapObject() called but _mapObject is null");
    }
    return *_mapObject; //.get();
}

bool Object::hasMapObject() const noexcept {
    return _mapObject != nullptr;
}

void Object::setMapObject(std::shared_ptr<MapObject> newMapObject) {
    _mapObject = newMapObject;
}

void Object::setSprite(sf::Sprite sprite) {
    bool wasSelected = _selected;
    _sprite = sprite;
    
    // Preserve selection state by re-applying selection color if the object was selected
    if (wasSelected) {
        _sprite.setColor(geck::ColorUtils::createObjectSelectionColor());
    }
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
    
    // Reset direction to 0 and update texture rectangle
    _direction = 0;
    setDirection(ObjectDirection(_direction));
}

void Object::setHexPosition(const Hex& hex) {

    // center on the hex
    WorldCoords position(
        hex.x() - (width() / 2) + shiftX(),
        hex.y() - height() + shiftY()
    );

    _sprite.setPosition(position.toVector());
    if (_mapObject != nullptr) {
        _mapObject->position = hex.position();
    }
    
    // Update light overlay position if needed
    if (_showLightOverlay && hasLight()) {
        updateLightOverlay();
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

    // FIXME: ??? one scrblk on arcaves.map
    if (_frm->directions().size() <= static_cast<size_t>(_direction) || _direction < 0) {
        spdlog::error("Object has orientation index {} but the FRM has only [{}] orientations", _direction, _frm->directions().size());
        _direction = 0;
    }

    if (_mapObject != nullptr) {
        _mapObject->direction = _direction;
    }

    // Take the first frame of the direction
    auto first_frame = _frm->directions().at(_direction).frames().at(0);

    uint16_t left = 0;
    uint16_t top = _direction * _frm->maxFrameHeight();
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
    _sprite.setColor(geck::ColorUtils::createObjectSelectionColor());
    _selected = true;
}

void Object::unselect() {
    _sprite.setColor(sf::Color::White);
    _selected = false;
}

bool Object::isSelected() const noexcept {
    return _selected;
}

void Object::initializeLightOverlay() {
    // Initialize the light overlay circle
    _lightOverlay.setFillColor(sf::Color(255, 255, 100, 30)); // Light yellow, semi-transparent
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
    
    // Calculate radius based on light_radius property
    // Each hex is approximately 32 pixels wide in standard zoom
    float hexWidth = 32.0f;
    float radius = _mapObject->light_radius * hexWidth / 2.0f;
    
    _lightOverlay.setRadius(radius);
    _lightOverlay.setOrigin(sf::Vector2f(radius, radius));
    
    // Position the overlay at the object's center
    auto spritePos = _sprite.getPosition();
    auto spriteBounds = _sprite.getLocalBounds();
    _lightOverlay.setPosition(sf::Vector2f(
        spritePos.x + spriteBounds.size.x / 2.0f,
        spritePos.y + spriteBounds.size.y / 2.0f
    ));
    
    // Adjust opacity based on light intensity
    auto color = _lightOverlay.getFillColor();
    // Map intensity (0-65535) to alpha (30-100)
    uint8_t alpha = static_cast<uint8_t>(30 + (_mapObject->light_intensity / 65535.0f) * 70);
    color.a = alpha;
    _lightOverlay.setFillColor(color);
}

bool Object::hasLight() const noexcept {
    return _mapObject && _mapObject->isLightSource();
}

} // namespace geck
