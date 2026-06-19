#pragma once

#include <SFML/Graphics.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace geck {

class Map;
class Object;
class ScriptEditService;
class UndoBatcher;

/**
 * @brief Map-wide editing operations.
 *
 * One of the aggregate services ObjectCommandController delegates to: resetting
 * the map to a blank slate, clearing an elevation's objects, and copying one
 * elevation onto another. Clearing/copying touch scripts, so the service borrows
 * ScriptEditService for the section snapshot/restore/erase machinery.
 */
class MapEditService {
public:
    MapEditService(std::unique_ptr<Map>& map,
        std::vector<std::shared_ptr<Object>>& objects,
        std::vector<sf::Sprite>& wallBlockerOverlays,
        ScriptEditService& scriptService,
        UndoBatcher& batcher,
        std::function<void()> refreshObjects,
        std::function<void()> reloadTiles);

    /// Replaces the bound map with a fresh empty Fallout 2 map and refreshes the
    /// view. Destructive reset (like File > New); NOT recorded on the undo stack.
    void newEmptyMap();

    /// Deletes every object on an elevation as one undoable command (pruning their
    /// scripts). Returns false if the elevation has no objects.
    bool clearElevationObjects(int elevation);

    /// Copies tiles + objects (deep-cloned, scripts detached) from one elevation to
    /// another as one undoable command, overwriting the destination.
    bool copyElevation(int fromElevation, int toElevation);

private:
    std::unique_ptr<Map>& _map;
    std::vector<std::shared_ptr<Object>>& _objects;
    std::vector<sf::Sprite>& _wallBlockerOverlays;
    ScriptEditService& _scriptService;
    UndoBatcher& _batcher;
    std::function<void()> _refreshObjects;
    std::function<void()> _reloadTiles;
};

} // namespace geck
