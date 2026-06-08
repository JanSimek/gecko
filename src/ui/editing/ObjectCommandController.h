#pragma once

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "util/UndoStack.h"

namespace geck {

class HexagonGrid;
class Map;
class MapSpriteLoader;
class Object;
class Tile;
struct MapObject;
struct TileChange;

namespace resource {
    class GameResources;
}

struct ExitGridCommandState {
    uint32_t exitMap;
    uint32_t exitPosition;
    uint32_t exitElevation;
    uint32_t exitOrientation;
    uint32_t frmPid;
    uint32_t proPid;
};

class ObjectCommandController {
public:
    ObjectCommandController(resource::GameResources& resources,
        std::unique_ptr<Map>& map,
        const HexagonGrid& hexgrid,
        MapSpriteLoader& mapSpriteLoader,
        std::vector<std::shared_ptr<Object>>& objects,
        std::vector<sf::Sprite>& wallBlockerOverlays,
        UndoStack& undoStack,
        std::function<void()> refreshObjects,
        std::function<void()> onStackChanged,
        std::function<std::vector<Tile>&(int)> ensureElevationTiles,
        std::function<int()> getCurrentElevation,
        std::function<void(int, bool, int)> updateTileSprite);

    void addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    void removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);

    /// Pushes a command onto the shared undo stack and notifies (the single owner
    /// of "a command was recorded"). All register*() helpers funnel through here.
    bool pushCommand(UndoCommand cmd);

    bool registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    bool registerObjectDeletion(const std::vector<std::pair<std::shared_ptr<MapObject>, std::shared_ptr<Object>>>& removedObjects);
    bool registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<std::pair<int, int>>& moves);
    bool registerObjectRotation(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<int>& beforeDirs, const std::vector<int>& afterDirs);
    void applyFrmToObject(const std::shared_ptr<Object>& object, uint32_t frmPid, const std::string& frmPath);
    bool registerObjectFrmChange(const std::shared_ptr<Object>& object, uint32_t oldFrmPid, const std::string& oldFrmPath, uint32_t newFrmPid, const std::string& newFrmPath);
    bool registerExitGridCreation(const std::vector<std::shared_ptr<MapObject>>& exitGrids, int elevation);
    bool registerExitGridEdit(const std::vector<std::shared_ptr<MapObject>>& exitGrids,
        const std::vector<ExitGridCommandState>& beforeStates,
        const std::vector<ExitGridCommandState>& afterStates);

    /// Applies the before/after state of tile edits and refreshes affected sprites.
    void applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState);
    /// Records an undoable tile edit (the change was already applied by the caller).
    void registerTileEdit(const std::string& description, const std::vector<TileChange>& changes);

private:
    resource::GameResources& _resources;
    std::unique_ptr<Map>& _map;
    const HexagonGrid& _hexgrid;
    MapSpriteLoader& _mapSpriteLoader;
    std::vector<std::shared_ptr<Object>>& _objects;
    std::vector<sf::Sprite>& _wallBlockerOverlays;
    UndoStack& _undoStack;
    std::function<void()> _refreshObjects;
    std::function<void()> _onStackChanged;
    std::function<std::vector<Tile>&(int)> _ensureElevationTiles;
    std::function<int()> _getCurrentElevation;
    std::function<void(int, bool, int)> _updateTileSprite;
};

} // namespace geck
