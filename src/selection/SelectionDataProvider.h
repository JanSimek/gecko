#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <optional>
#include <vector>

#include "format/map/Map.h"

namespace geck {
class Object;
class HexagonGrid;
class ViewportController;
}

namespace geck::selection {

/**
 * @brief Narrow abstraction over the editor host that SelectionManager depends on.
 *
 * SelectionManager used to hold a raw EditorWidget* (and include EditorWidget.h),
 * which created a compile cycle and made the manager untestable in isolation.
 * This interface declares exactly the methods SelectionManager needs from its host;
 * EditorWidget implements it, and tests can supply a lightweight mock.
 *
 * Heavy types are forward-declared above; Map::MapFile is a nested type and cannot
 * be forward-declared, so Map.h is included (SelectionManager.h includes it already).
 */
class SelectionDataProvider {
public:
    virtual ~SelectionDataProvider() = default;

    // --- Data accessors ---
    virtual const std::vector<std::shared_ptr<Object>>& getObjects() const = 0;
    virtual const std::vector<sf::Sprite>& getFloorSprites() const = 0;
    virtual const std::vector<sf::Sprite>& getRoofSprites() const = 0;
    virtual const HexagonGrid* getHexagonGrid() const = 0;
    virtual Map::MapFile& getMapFile() = 0;
    virtual const Map::MapFile& getMapFile() const = 0;
    virtual int getCurrentElevation() const = 0;
    virtual ViewportController* getViewportController() const = 0;

    // --- Hit tests ---
    // SelectionManager wraps these in its own elevation-aware private helpers.
    virtual std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos) = 0;
    virtual std::optional<int> getTileAtPosition(sf::Vector2f worldPos, bool isRoof) = 0;
    virtual std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) = 0;
};

} // namespace geck::selection
