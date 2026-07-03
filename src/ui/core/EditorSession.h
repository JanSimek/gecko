#pragma once

#include <SFML/Graphics.hpp>

#include <filesystem>
#include <memory>
#include <vector>

#include "VisibilitySettings.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "selection/SelectionManager.h"
#include "util/UndoStack.h"

namespace geck {

/**
 * @brief Mutable editing state for one open map.
 *
 * Holds the per-session state the editor mutates while a map is open: the map
 * document and current elevation, the hex grid, the live object model and its
 * rendered sprite caches, the selection manager, the undo history, and the
 * layer-visibility settings.
 *
 * EditorSession is Qt-free: EditorWidget owns one and hands references to it (or
 * to the data it holds) to the rendering, selection, and command collaborators.
 */
class EditorSession final {
public:
    // The open map. map() is the common accessor; mapPtr() exposes the owning
    // pointer for collaborators that must observe reassignment (new/load map).
    [[nodiscard]] Map* map() const { return _map.get(); }
    [[nodiscard]] std::unique_ptr<Map>& mapPtr() { return _map; }
    void setMap(std::unique_ptr<Map> map) { _map = std::move(map); }

    /// Replaces the open map with a fresh empty one and clears all per-map session state
    /// (objects, floor/roof sprites, wall-blocker overlays, elevation). The view-side concerns
    /// — sprite reload, selection, camera, MainWindow refresh — remain the caller's (File > New).
    void resetToEmptyMap() {
        setMap(std::make_unique<Map>(std::filesystem::path("newmap.map")));
        _map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
        setCurrentElevation(0);
        _objects.clear();
        _floorSprites.clear();
        _roofSprites.clear();
        _wallBlockerOverlays.clear();
        _selectedSpatialScriptSid = MapScript::NONE;
    }

    [[nodiscard]] VisibilitySettings& visibility() { return _visibility; }
    [[nodiscard]] const VisibilitySettings& visibility() const { return _visibility; }

    // The SID (MapScript::pid) of the spatial script currently selected on the map / in the
    // Scripts panel, or MapScript::NONE. Shared by the click hit-test, the panel row, and the
    // renderer (which highlights the matching marker).
    [[nodiscard]] uint32_t selectedSpatialScriptSid() const { return _selectedSpatialScriptSid; }
    void setSelectedSpatialScriptSid(uint32_t sid) { _selectedSpatialScriptSid = sid; }

    [[nodiscard]] UndoStack& undoStack() { return _undoStack; }
    [[nodiscard]] const UndoStack& undoStack() const { return _undoStack; }

    [[nodiscard]] int currentElevation() const { return _currentElevation; }
    void setCurrentElevation(int elevation) { _currentElevation = elevation; }

    [[nodiscard]] selection::SelectionManager* selectionManager() const { return _selectionManager.get(); }
    void setSelectionManager(std::unique_ptr<selection::SelectionManager> manager) { _selectionManager = std::move(manager); }

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
    std::unique_ptr<Map> _map;

    VisibilitySettings _visibility;
    UndoStack _undoStack{ 100 };

    int _currentElevation = 0;

    uint32_t _selectedSpatialScriptSid = MapScript::NONE;

    std::unique_ptr<selection::SelectionManager> _selectionManager;

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
