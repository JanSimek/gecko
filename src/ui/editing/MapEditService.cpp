#include "MapEditService.h"

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "ui/editing/ScriptEditService.h"
#include "ui/editing/UndoBatcher.h"
#include "util/UndoStack.h"

namespace geck {

MapEditService::MapEditService(std::unique_ptr<Map>& map,
    std::vector<std::shared_ptr<Object>>& objects,
    std::vector<sf::Sprite>& wallBlockerOverlays,
    ScriptEditService& scriptService,
    UndoBatcher& batcher,
    std::function<void()> refreshObjects,
    std::function<void()> reloadTiles)
    : _map(map)
    , _objects(objects)
    , _wallBlockerOverlays(wallBlockerOverlays)
    , _scriptService(scriptService)
    , _batcher(batcher)
    , _refreshObjects(std::move(refreshObjects))
    , _reloadTiles(std::move(reloadTiles)) {
}

void MapEditService::newEmptyMap() {
    if (!_map) {
        return;
    }
    // Swap in a blank MapFile, then drop the cached object/overlay sprites and rebuild the view from
    // the now-empty map. Intentionally not undoable (a fresh start, like File > New).
    _map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
    _objects.clear();
    _wallBlockerOverlays.clear();
    _refreshObjects();
    _reloadTiles();
}

bool MapEditService::clearElevationObjects(int elevation) {
    if (!_map) {
        return false;
    }
    auto& mapFile = _map->getMapFile();
    auto it = mapFile.map_objects.find(elevation);
    if (it == mapFile.map_objects.end() || it->second.empty()) {
        return false;
    }

    // shared_ptr copy keeps the removed objects alive in the command for undo.
    std::vector<std::shared_ptr<MapObject>> before = it->second;
    std::vector<std::shared_ptr<MapObject>> after; // empty

    // Removing the objects must also remove their attached scripts, otherwise the
    // map keeps orphaned MapScript records pointing at OIDs that no longer exist.
    // Prune by SID so the objects' own map_scripts_pid stays intact for undo.
    ScriptEditService::ScriptSections beforeScripts = _scriptService.snapshotScripts();
    it->second.clear();
    for (const auto& obj : before) {
        if (obj && obj->map_scripts_pid != -1) {
            _scriptService.eraseScript(static_cast<uint32_t>(obj->map_scripts_pid));
        }
    }
    ScriptEditService::ScriptSections afterScripts = _scriptService.snapshotScripts();

    UndoCommand cmd;
    cmd.description = "Clear Elevation Objects";
    cmd.undo = [this, elevation, before, beforeScripts]() {
        _map->getMapFile().map_objects[elevation] = before;
        _scriptService.restoreScripts(beforeScripts);
        _refreshObjects();
    };
    cmd.redo = [this, elevation, after, afterScripts]() {
        _map->getMapFile().map_objects[elevation] = after;
        _scriptService.restoreScripts(afterScripts);
        _refreshObjects();
    };
    // The edit was already applied above; only register the undo/redo handlers.
    return _batcher.push(std::move(cmd));
}

bool MapEditService::copyElevation(int fromElevation, int toElevation) {
    if (!_map || fromElevation == toElevation) {
        return false;
    }
    auto& mapFile = _map->getMapFile();

    // Destination "before": share ownership of existing objects + copy its tiles.
    std::vector<std::shared_ptr<MapObject>> beforeObjects;
    if (auto it = mapFile.map_objects.find(toElevation); it != mapFile.map_objects.end()) {
        beforeObjects = it->second;
    }
    std::vector<Tile> beforeTiles;
    bool haveBeforeTiles = false;
    if (auto it = mapFile.tiles.find(toElevation); it != mapFile.tiles.end()) {
        beforeTiles = it->second;
        haveBeforeTiles = true;
    }

    // Destination "after": deep-cloned source objects (retargeted) + source tiles.
    std::vector<std::shared_ptr<MapObject>> afterObjects;
    if (auto it = mapFile.map_objects.find(fromElevation); it != mapFile.map_objects.end()) {
        afterObjects.reserve(it->second.size());
        for (const auto& o : it->second) {
            if (!o) {
                continue;
            }
            auto clone = std::shared_ptr<MapObject>(o->cloneDeep());
            clone->elevation = static_cast<uint32_t>(toElevation);
            // Scripts are not copied, so detach the clone's linkage; otherwise it
            // would duplicate the source object's OID and share its SID/script.
            clone->map_scripts_pid = -1;
            clone->script_id = -1;
            clone->unknown0 = 0;
            afterObjects.push_back(std::move(clone));
        }
    }
    std::vector<Tile> afterTiles;
    bool haveAfterTiles = false;
    if (auto it = mapFile.tiles.find(fromElevation); it != mapFile.tiles.end()) {
        afterTiles = it->second;
        haveAfterTiles = true;
    }

    UndoCommand cmd;
    cmd.description = "Copy Elevation";
    cmd.undo = [this, toElevation, beforeObjects, beforeTiles, haveBeforeTiles]() {
        auto& mf = _map->getMapFile();
        mf.map_objects[toElevation] = beforeObjects;
        if (haveBeforeTiles) {
            mf.tiles[toElevation] = beforeTiles;
        }
        _refreshObjects();
        if (_reloadTiles) {
            _reloadTiles();
        }
    };
    cmd.redo = [this, toElevation, afterObjects, afterTiles, haveAfterTiles]() {
        auto& mf = _map->getMapFile();
        mf.map_objects[toElevation] = afterObjects;
        if (haveAfterTiles) {
            mf.tiles[toElevation] = afterTiles;
        }
        _refreshObjects();
        if (_reloadTiles) {
            _reloadTiles();
        }
    };
    cmd.redo();
    return _batcher.push(std::move(cmd));
}

} // namespace geck
