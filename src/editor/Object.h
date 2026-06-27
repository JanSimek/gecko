#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    [[nodiscard]] std::shared_ptr<MapObject> getMapObjectPtr() const noexcept;
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

    /// The exit-grid direction (0..7) of this object, or -1 if it is not an exit-grid marker. Real
    /// markers report it from their MapObject proto index; preview/bare-FRM objects from the art name.
    /// Used by the renderer to detect and widen DIAGONAL exit-grid bars (display-only).
    [[nodiscard]] int exitGridDirection() const;

private:
    static sf::Texture& createBlankTexture();
    void initializeLightOverlay();

    /// EDITOR-DISPLAY ONLY: nudge an exit-grid marker's sprite OUTWARD so the trigger `hex` sits at the
    /// bar's inner (player-facing) edge. No-op for every non-exit-grid object. See setHexPosition().
    void applyExitGridOutwardOffset(const Hex& hex);
};

} // namespace geck
