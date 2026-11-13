#include "MapEditService.h"

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "editing/commands/ScriptEditService.h"
#include "editing/commands/UndoBatcher.h"
#include "util/BuiltTile.h"
#include "util/UndoStack.h"

namespace geck {

MapEditService::MapEditService(std::unique_ptr<Map>& map,
    std::vector<std::shared_ptr<Object>>& objects,
    std::vector<sf::Sprite>& wallBlockerOverlays,
    ScriptEditService& scriptService,
    UndoBatcher& batcher,
    Hooks hooks)
    : _map(map)
    , _objects(objects)
    , _wallBlockerOverlays(wallBlockerOverlays)
    , _scriptService(scriptService)
    , _batcher(batcher)
    , _refreshObjects(std::move(hooks.refreshObjects))
    , _reloadTiles(std::move(hooks.reloadTiles))
    , _headerChanged(std::move(hooks.headerChanged)) {
}

void MapEditService::newEmptyMap() {
    if (!_map) {
        return;
    }
    // Swap in a blank MapFile, then drop the cached object/overlay sprites and rebuild the view from
    // the now-empty map. Intentionally not undoable (a fresh start, like File > New), and it drops
    // the history recorded against the old map.
    _map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
    _batcher.clearHistory();
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

std::vector<std::shared_ptr<MapObject>> MapEditService::cloneElevationObjects(int fromElevation, int toElevation) {
    std::vector<std::shared_ptr<MapObject>> clones;
    auto& mapFile = _map->getMapFile();
    auto it = mapFile.map_objects.find(fromElevation);
    if (it == mapFile.map_objects.end()) {
        return clones;
    }
    clones.reserve(it->second.size());
    for (const auto& o : it->second) {
        if (!o) {
            continue;
        }
        auto clone = std::shared_ptr<MapObject>(o->cloneDeep());
        clone->elevation = static_cast<uint32_t>(toElevation);
        // Scripts are not copied, so detach the clone's linkage; otherwise it would
        // duplicate the source object's OID and share its SID/script.
        clone->map_scripts_pid = -1;
        clone->script_id = -1;
        clone->unknown0 = 0;
        clones.push_back(std::move(clone));
    }
    return clones;
}

void MapEditService::applyElevationSnapshot(int elevation, const std::vector<std::shared_ptr<MapObject>>& objects,
    const std::vector<Tile>& tiles, bool haveTiles) {
    auto& mapFile = _map->getMapFile();
    mapFile.map_objects[elevation] = objects;
    if (haveTiles) {
        mapFile.tiles[elevation] = tiles;
    }
    _refreshObjects();
    if (_reloadTiles) {
        _reloadTiles();
    }
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
    std::vector<std::shared_ptr<MapObject>> afterObjects = cloneElevationObjects(fromElevation, toElevation);
    std::vector<Tile> afterTiles;
    bool haveAfterTiles = false;
    if (auto it = mapFile.tiles.find(fromElevation); it != mapFile.tiles.end()) {
        afterTiles = it->second;
        haveAfterTiles = true;
    }

    UndoCommand cmd;
    cmd.description = "Copy Elevation";
    cmd.undo = [this, toElevation, beforeObjects, beforeTiles, haveBeforeTiles]() {
        applyElevationSnapshot(toElevation, beforeObjects, beforeTiles, haveBeforeTiles);
    };
    cmd.redo = [this, toElevation, afterObjects, afterTiles, haveAfterTiles]() {
        applyElevationSnapshot(toElevation, afterObjects, afterTiles, haveAfterTiles);
    };
    cmd.redo();
    return _batcher.push(std::move(cmd));
}

bool MapEditService::setHeader(const Map::MapHeader& after, std::string_view description, std::string_view mergeKey) {
    if (!_map) {
        return false;
    }
    const Map::MapHeader before = _map->getMapFile().header;
    if (before == after) {
        return false;
    }

    auto apply = [this](const Map::MapHeader& header) {
        _map->getMapFile().header = header;
        if (_headerChanged) {
            _headerChanged();
        }
    };
    UndoCommand cmd;
    cmd.description = description;
    cmd.mergeKey = mergeKey.empty() ? std::string() : std::string("header:").append(mergeKey);
    cmd.undo = [apply, before]() { apply(before); };
    cmd.redo = [apply, after]() { apply(after); };
    apply(after);
    return _batcher.push(std::move(cmd));
}

MapEditService::ElevationState MapEditService::captureElevation(int elevation) const {
    const auto& mapFile = _map->getMapFile();
    ElevationState state;
    state.flags = mapFile.header.flags;
    if (auto it = mapFile.tiles.find(elevation); it != mapFile.tiles.end()) {
        state.tiles = it->second;
    }
    if (auto it = mapFile.map_objects.find(elevation); it != mapFile.map_objects.end()) {
        state.objects = it->second; // shared_ptr copies keep removed objects alive for undo
    }
    return state;
}

void MapEditService::applyElevationState(int elevation, const ElevationState& state) {
    auto& mapFile = _map->getMapFile();
    mapFile.header.flags = state.flags;
    if (state.tiles) {
        mapFile.tiles[elevation] = *state.tiles;
    } else {
        mapFile.tiles.erase(elevation);
    }
    if (state.objects) {
        mapFile.map_objects[elevation] = *state.objects;
    } else {
        mapFile.map_objects.erase(elevation);
    }
}

bool MapEditService::canToggleElevation(int elevation, bool present) const {
    if (!_map || elevation < 0 || elevation >= Map::ELEVATION_COUNT) {
        return false;
    }
    const uint32_t flags = _map->getMapFile().header.flags;
    if (Map::elevationIsPresent(flags, elevation) == present) {
        return false;
    }
    if (present) {
        return true;
    }
    int presentCount = 0;
    for (int e = 0; e < Map::ELEVATION_COUNT; ++e) {
        presentCount += Map::elevationIsPresent(flags, e) ? 1 : 0;
    }
    return presentCount > 1; // a map needs at least one elevation
}

void MapEditService::addElevation(int elevation) {
    auto& mapFile = _map->getMapFile();
    mapFile.header.flags &= ~Map::elevationFlag(elevation); // the bit is clear when the elevation is present
    if (!mapFile.tiles.contains(elevation)) {
        mapFile.tiles[elevation] = Map::createEmptyElevation();
    }
    mapFile.map_objects.try_emplace(elevation);
}

void MapEditService::removeElevation(int elevation, const std::optional<std::vector<std::shared_ptr<MapObject>>>& objects) {
    auto& mapFile = _map->getMapFile();
    mapFile.header.flags |= Map::elevationFlag(elevation);
    for (const auto& obj : objects.value_or(std::vector<std::shared_ptr<MapObject>>{})) {
        if (obj && obj->map_scripts_pid != -1) {
            _scriptService.eraseScript(static_cast<uint32_t>(obj->map_scripts_pid));
        }
    }
    const auto& spatial = mapFile.map_scripts[static_cast<int>(MapScript::ScriptType::SPATIAL)];
    std::vector<uint32_t> spatialOnElevation;
    for (const MapScript& script : spatial) {
        if (static_cast<int>(built_tile::elevationOf(script.timer)) == elevation) {
            spatialOnElevation.push_back(script.pid);
        }
    }
    for (uint32_t sid : spatialOnElevation) {
        _scriptService.eraseScript(sid);
    }
    mapFile.tiles.erase(elevation);
    mapFile.map_objects.erase(elevation);
}

bool MapEditService::setElevationPresent(int elevation, bool present) {
    if (!canToggleElevation(elevation, present)) {
        return false;
    }

    const ElevationState before = captureElevation(elevation);
    const ScriptEditService::ScriptSections beforeScripts = _scriptService.snapshotScripts();
    if (present) {
        addElevation(elevation);
    } else {
        removeElevation(elevation, before.objects);
    }

    const ElevationState after = captureElevation(elevation);
    const ScriptEditService::ScriptSections afterScripts = _scriptService.snapshotScripts();

    auto refresh = [this]() {
        _refreshObjects();
        if (_reloadTiles) {
            _reloadTiles();
        }
        if (_headerChanged) {
            _headerChanged();
        }
    };
    UndoCommand cmd;
    cmd.description = present ? "Add Elevation" : "Remove Elevation";
    cmd.undo = [this, elevation, before, beforeScripts, refresh]() {
        applyElevationState(elevation, before);
        _scriptService.restoreScripts(beforeScripts);
        refresh();
    };
    cmd.redo = [this, elevation, after, afterScripts, refresh]() {
        applyElevationState(elevation, after);
        _scriptService.restoreScripts(afterScripts);
        refresh();
    };
    refresh();
    return _batcher.push(std::move(cmd));
}

} // namespace geck
