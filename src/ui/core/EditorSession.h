#pragma once

#include <SFML/Graphics.hpp>

#include <memory>
#include <vector>

#include "VisibilitySettings.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "util/UndoStack.h"

namespace geck {

/**
 * @brief Mutable editing state for one open map.
 *
 * Holds the per-session state the editor mutates while a map is open: the hex
 * grid, the live object model and its rendered sprite caches, the undo history,
 * and the layer-visibility settings. The remaining document data (the Map, the
 * current elevation, the selection) migrates here incrementally as part of the
 * EditorWidget/MainWindow orchestration split.
 *
 * EditorSession is Qt-free: EditorWidget owns one and hands references to it (or
 * to the data it holds) to the rendering, selection, and command collaborators.
 */
class EditorSession final {
public:
    [[nodiscard]] VisibilitySettings& visibility() { return _visibility; }
    [[nodiscard]] const VisibilitySettings& visibility() const { return _visibility; }

    [[nodiscard]] UndoStack& undoStack() { return _undoStack; }
    [[nodiscard]] const UndoStack& undoStack() const { return _undoStack; }

    [[nodiscard]] int currentElevation() const { return _currentElevation; }
    void setCurrentElevation(int elevation) { _currentElevation = elevation; }

    [[nodiscard]] HexagonGrid& hexgrid() { return _hexgrid; }
    [[nodiscard]] const HexagonGrid& hexgrid() const { return _hexgrid; }

    [[nodiscard]] std::vector<std::shared_ptr<Object>>& objects() { return _objects; }
    [[nodiscard]] const std::vector<std::shared_ptr<Object>>& objects() const { return _objects; }

    [[nodiscard]] std::vector<sf::Sprite>& floorSprites() { return _floorSprites; }
    [[nodiscard]] const std::vector<sf::Sprite>& floorSprites() const { return _floorSprites; }

    [[nodiscard]] std::vector<sf::Sprite>& roofSprites() { return _roofSprites; }
    [[nodiscard]] const std::vector<sf::Sprite>& roofSprites() const { return _roofSprites; }

    [[nodiscard]] std::vector<sf::Sprite>& wallBlockerOverlays() { return _wallBlockerOverlays; }
    [[nodiscard]] const std::vector<sf::Sprite>& wallBlockerOverlays() const { return _wallBlockerOverlays; }

private:
    VisibilitySettings _visibility;
    UndoStack _undoStack{ 100 };

    int _currentElevation = 0;

    HexagonGrid _hexgrid;

    std::vector<std::shared_ptr<Object>> _objects;

    // Note: std::vector (not std::array) because SFML 3 sf::Sprite requires a
    // texture at construction and is not default-constructible.
    std::vector<sf::Sprite> _floorSprites;
    std::vector<sf::Sprite> _roofSprites;

    // Wall-blocker overlay sprites, drawn on top of regular objects.
    std::vector<sf::Sprite> _wallBlockerOverlays;
};

} // namespace geck
