#pragma once

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "util/UndoStack.h"
#include "editing/commands/UndoBatcher.h"
#include "editing/commands/TileEditService.h"
#include "editing/commands/InventoryEditService.h"
#include "editing/commands/ScriptEditService.h"
#include "editing/commands/MapEditService.h"
#include "editing/commands/ObjectEditService.h"

namespace geck {

class CommandHost;
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

// ExitGridCommandState and MapObjectInstanceState are defined in ObjectEditService.h
// (the aggregate that owns exit-grid and per-instance editing) and re-exported here.

class ObjectCommandController {
public:
    /// @param host presentation/event sink notified as edits are applied (see CommandHost).
    ///        Must outlive this controller.
    ObjectCommandController(resource::GameResources& resources,
        std::unique_ptr<Map>& map,
        const HexagonGrid& hexgrid,
        MapSpriteLoader& mapSpriteLoader,
        std::vector<std::shared_ptr<Object>>& objects,
        std::vector<sf::Sprite>& wallBlockerOverlays,
        UndoStack& undoStack,
        CommandHost& host);

    void addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    void removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);

    /// Add/remove a MapObject in the map data only — no sprite, no MapSpriteLoader. The
    /// headless counterpart to add/removePlacedObject: usable without a GL context (CLI,
    /// scripts), since a MapObject is pure data the engine renders at load time.
    void addObjectData(const std::shared_ptr<MapObject>& mapObject);
    void removeObjectData(const std::shared_ptr<MapObject>& mapObject);

    /// Pushes a command onto the shared undo stack and notifies (the single owner
    /// of "a command was recorded"). All register*() helpers funnel through here.
    /// While a batch is open (see beginBatch), commands are buffered instead of
    /// pushed so the whole batch lands as a single undo entry.
    bool pushCommand(UndoCommand cmd);

    /// Opens an undo batch: subsequent register*()/pushCommand() calls buffer their
    /// commands instead of pushing them, and endBatch() collapses the whole run into
    /// ONE UndoStack entry (replayed/reverted as a unit). This keeps multi-edit
    /// operations — area paste, prefab stamp, procedural fill — from blowing the
    /// UndoStack's command cap and forcing one Ctrl-Z per hex. Batches nest: only the
    /// outermost endBatch() flushes, and the outermost description is the one used.
    /// The individual edits are still applied immediately by the register*() calls;
    /// the batch only governs how they are recorded for undo/redo.
    void beginBatch(const std::string& description);

    /// Closes the batch opened by the matching beginBatch(). On the outermost close,
    /// buffered commands are collapsed into a single UndoCommand and pushed (one
    /// stack-changed notification). Returns true iff a command was pushed (false for
    /// an empty batch, an inner nested close, or an unbalanced call).
    bool endBatch();

    /// True while at least one beginBatch() is open.
    bool isBatching() const { return _batcher.isBatching(); }

    bool registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    /// Undoable placement that records the MapObject data only (no sprite). For headless
    /// callers (gecko-cli, generation scripts run without a GL context); the GUI uses
    /// registerObjectPlacement so the object is also drawn.
    bool registerObjectData(const std::shared_ptr<MapObject>& mapObject);
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
    /// Applies a tile edit now (refreshing sprites) and records it for undo/redo.
    void applyTileEdit(const std::string& description, const std::vector<TileChange>& changes);

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
    /// undoable command, overwriting the destination. Scripts are not copied; the
    /// cloned objects are detached from any source scripts.
    bool copyElevation(int fromElevation, int toElevation);

    /// Replaces the bound map with a fresh empty Fallout 2 map (default header, three
    /// empty elevations, no objects/scripts) and refreshes the view. This is a
    /// destructive reset — like File > New, it is NOT recorded on the undo stack — so a
    /// generation script can start from a blank slate regardless of what was loaded.
    void newEmptyMap();

    // Script attachment (undoable). `scriptType` is the MapScript section/type
    // (ITEM for items/scenery/walls, CRITTER for critters); `programIndex` is the
    // scripts.lst line. attachScript replaces any existing script on the object.
    bool attachScript(const std::shared_ptr<MapObject>& object, int scriptType, uint32_t programIndex);
    bool detachScript(const std::shared_ptr<MapObject>& object);
    bool addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius);
    // Edit / remove an existing spatial script by SID (undoable); findSpatialScript reads its
    // current values (e.g. to pre-fill an edit dialog). See ScriptEditService for details.
    bool editSpatialScript(uint32_t sid, uint32_t programIndex, int tile, int elevation, int radius);
    bool removeSpatialScript(uint32_t sid);
    const MapScript* findSpatialScript(uint32_t sid) const;

private:
    resource::GameResources& _resources;
    std::unique_ptr<Map>& _map;
    const HexagonGrid& _hexgrid;
    MapSpriteLoader& _mapSpriteLoader;
    std::vector<std::shared_ptr<Object>>& _objects;
    std::vector<sf::Sprite>& _wallBlockerOverlays;
    CommandHost& _host;
    UndoBatcher _batcher;
    TileEditService _tileService;
    InventoryEditService _inventoryService;
    ScriptEditService _scriptService;
    std::function<void()> _refreshObjects;
    std::function<void()> _reloadTiles;
    MapEditService _mapService;
    ObjectEditService _objectService;
};

/// RAII guard that opens an undo batch for its lifetime: collapses every edit made
/// within the scope into a single undo entry, and flushes even if the scope exits
/// via an exception (e.g. a sandboxed script aborting mid-generation). Prefer this
/// over manual beginBatch()/endBatch() at call sites.
class ScopedUndoBatch {
public:
    ScopedUndoBatch(ObjectCommandController& controller, const std::string& description)
        : _controller(controller) {
        _controller.beginBatch(description);
    }
    ~ScopedUndoBatch() {
        // endBatch() allocates (make_shared / std::function), so it can throw. A
        // throwing destructor during stack unwinding would std::terminate — which
        // would defeat the whole point of flushing safely when a scope exits via an
        // exception — so swallow any failure here. The edits were already applied;
        // at worst their batched undo record is dropped.
        try {
            _controller.endBatch();
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
    }

    ScopedUndoBatch(const ScopedUndoBatch&) = delete;
    ScopedUndoBatch& operator=(const ScopedUndoBatch&) = delete;
    ScopedUndoBatch(ScopedUndoBatch&&) = delete;
    ScopedUndoBatch& operator=(ScopedUndoBatch&&) = delete;

private:
    ObjectCommandController& _controller;
};

} // namespace geck
