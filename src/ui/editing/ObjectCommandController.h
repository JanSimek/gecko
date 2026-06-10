#pragma once

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "util/UndoStack.h"
#include "format/map/MapScript.h"

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

/// Snapshot of a MapObject's UI-editable per-instance fields, shared by the
/// flag/light/destination/interaction/critter editors via registerInstanceEdit.
struct MapObjectInstanceState {
    uint32_t flags = 0;
    uint32_t dataFlags = 0; // MapObject.unknown11 == engine obj->data.flags (container lock/jam)
    uint32_t lightRadius = 0;
    uint32_t lightIntensity = 0;
    uint32_t walkthrough = 0; // doors: engine obj->data.scenery.door.openFlags (lock/jam)
    uint32_t map = 0;
    uint32_t elevhex = 0;
    uint32_t elevtype = 0;
    uint32_t elevlevel = 0;
    uint32_t aiPacket = 0;
    uint32_t groupId = 0;
    uint32_t currentHp = 0;
    uint32_t currentRad = 0;
    uint32_t currentPoison = 0;
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
        std::function<void(int, bool, int)> updateTileSprite,
        std::function<void()> reloadTiles);

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

    /// Captures the editable per-instance fields of a MapObject.
    static MapObjectInstanceState captureInstanceState(const MapObject& object);

    /// Records an undoable change to a single object's per-instance fields. The
    /// after-state is applied immediately (redo); objects are refreshed so light
    /// overlays and flag-derived visuals update.
    bool registerInstanceEdit(const std::shared_ptr<MapObject>& mapObject,
        const MapObjectInstanceState& before,
        const MapObjectInstanceState& after,
        const std::string& description);

    /// Applies the before/after state of tile edits and refreshes affected sprites.
    void applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState);
    /// Records an undoable tile edit (the change was already applied by the caller).
    void registerTileEdit(const std::string& description, const std::vector<TileChange>& changes);

    /// Deep-clones a container/critter inventory into a detached snapshot (the
    /// inventory holds unique_ptrs, so a snapshot must clone).
    static std::vector<std::shared_ptr<MapObject>> cloneInventory(
        const std::vector<std::unique_ptr<MapObject>>& inventory);

    /// Records an undoable inventory change. The caller has already applied the
    /// edit; `before`/`after` are cloneInventory() snapshots taken around it.
    bool registerInventoryEdit(const std::shared_ptr<MapObject>& container,
        std::vector<std::shared_ptr<MapObject>> before,
        std::vector<std::shared_ptr<MapObject>> after);

    /// Deletes every object on an elevation as one undoable command. Returns false
    /// if the elevation has no objects.
    bool clearElevationObjects(int elevation);
    /// Copies tiles + objects (deep-cloned) from one elevation to another as one
    /// undoable command, overwriting the destination.
    bool copyElevation(int fromElevation, int toElevation);

    // Script attachment (undoable). `scriptType` is the MapScript section/type
    // (ITEM for items/scenery/walls, CRITTER for critters); `programIndex` is the
    // scripts.lst line. attachScript replaces any existing script on the object.
    bool attachScript(const std::shared_ptr<MapObject>& object, int scriptType, uint32_t programIndex);
    bool detachScript(const std::shared_ptr<MapObject>& object);
    bool addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius);

private:
    uint32_t allocateScriptId(int section) const;
    uint32_t allocateObjectId() const;
    void removeObjectScript(MapObject& object);
    void applyScriptSnapshot(int section, const std::shared_ptr<MapObject>& object,
        const std::vector<MapScript>& sectionScripts, uint32_t oid, int32_t sid);
    bool recordScriptEdit(const std::string& description, int section,
        const std::shared_ptr<MapObject>& object,
        std::vector<MapScript> beforeSection, uint32_t beforeOid, int32_t beforeSid);

    static void applyInstanceState(MapObject& object, const MapObjectInstanceState& state);

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
    std::function<void()> _reloadTiles;
};

} // namespace geck
