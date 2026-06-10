#include "ObjectCommandController.h"

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "resource/GameResources.h"
#include "ui/core/TileChange.h"
#include "util/TileUtils.h"
#include "util/UndoStack.h"
#include "ui/rendering/MapSpriteLoader.h"

#include <algorithm>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace geck {

ObjectCommandController::ObjectCommandController(resource::GameResources& resources,
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
    std::function<void()> reloadTiles)
    : _resources(resources)
    , _map(map)
    , _hexgrid(hexgrid)
    , _mapSpriteLoader(mapSpriteLoader)
    , _objects(objects)
    , _wallBlockerOverlays(wallBlockerOverlays)
    , _undoStack(undoStack)
    , _refreshObjects(std::move(refreshObjects))
    , _onStackChanged(std::move(onStackChanged))
    , _ensureElevationTiles(std::move(ensureElevationTiles))
    , _getCurrentElevation(std::move(getCurrentElevation))
    , _updateTileSprite(std::move(updateTileSprite))
    , _reloadTiles(std::move(reloadTiles)) {
}

void ObjectCommandController::applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState) {
    if (!_map) {
        return;
    }

    std::unordered_map<int, std::vector<const TileChange*>> changesByElevation;
    for (const auto& change : changes) {
        changesByElevation[change.elevation].push_back(&change);
    }

    // Apply all changes first, then update sprites in batch.
    for (const auto& [elevation, elevChanges] : changesByElevation) {
        auto& elevationTiles = _ensureElevationTiles(elevation);

        for (const auto* change : elevChanges) {
            if (change->tileIndex < 0 || change->tileIndex >= static_cast<int>(elevationTiles.size())) {
                continue;
            }

            uint16_t value = applyAfterState ? change->after : change->before;
            if (change->isRoof) {
                elevationTiles[change->tileIndex].setRoof(value);
            } else {
                elevationTiles[change->tileIndex].setFloor(value);
            }
        }

        if (elevation == _getCurrentElevation()) {
            for (const auto* change : elevChanges) {
                if (change->tileIndex >= 0 && change->tileIndex < static_cast<int>(elevationTiles.size())) {
                    int hexIndex = tileIndexToHexIndex(change->tileIndex);
                    _updateTileSprite(hexIndex, change->isRoof, elevation);
                }
            }
        }
    }
}

void ObjectCommandController::registerTileEdit(const std::string& description, const std::vector<TileChange>& changes) {
    if (changes.empty()) {
        return;
    }

    UndoCommand cmd;
    cmd.description = description;
    cmd.undo = [this, changes]() {
        applyTileChanges(changes, false);
    };
    cmd.redo = [this, changes]() {
        applyTileChanges(changes, true);
    };
    pushCommand(std::move(cmd));
}

void ObjectCommandController::addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    if (!mapObject || !object || !_map) {
        spdlog::warn("addPlacedObject: Invalid parameters - mapObject:{} object:{} map:{}",
            mapObject != nullptr, object != nullptr, _map != nullptr);
        return;
    }
    if (mapObject->elevation >= 3) {
        spdlog::error("addPlacedObject: Invalid elevation {}", mapObject->elevation);
        return;
    }

    auto& mapFile = _map->getMapFile();
    mapFile.map_objects[mapObject->elevation].push_back(mapObject);
    _mapSpriteLoader.appendObjectSprite(mapObject, object, _objects, _wallBlockerOverlays);
}

void ObjectCommandController::removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    if (!mapObject || !_map) {
        spdlog::warn("removePlacedObject: Invalid parameters - mapObject:{} map:{}",
            mapObject != nullptr, _map != nullptr);
        return;
    }
    if (mapObject->elevation >= 3) {
        spdlog::error("removePlacedObject: Invalid elevation {}", mapObject->elevation);
        return;
    }

    auto& mapFile = _map->getMapFile();
    auto& elevationObjects = mapFile.map_objects[mapObject->elevation];
    elevationObjects.erase(std::remove(elevationObjects.begin(), elevationObjects.end(), mapObject), elevationObjects.end());
    if (object) {
        _objects.erase(std::remove(_objects.begin(), _objects.end(), object), _objects.end());
    }
    _refreshObjects();
}

bool ObjectCommandController::registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    if (!mapObject || !object) {
        spdlog::warn("registerObjectPlacement: Invalid parameters - mapObject:{} object:{}",
            mapObject != nullptr, object != nullptr);
        return false;
    }

    UndoCommand cmd;
    cmd.description = "Place Object";
    cmd.redo = [this, mapObject, object]() {
        if (mapObject && object) {
            addPlacedObject(mapObject, object);
        }
    };
    cmd.undo = [this, mapObject, object]() {
        if (mapObject && object) {
            removePlacedObject(mapObject, object);
        }
    };

    cmd.redo();
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::registerObjectDeletion(
    const std::vector<std::pair<std::shared_ptr<MapObject>, std::shared_ptr<Object>>>& removedObjects) {
    if (removedObjects.empty()) {
        return false;
    }

    // The objects are already removed by the caller; this only records undo/redo.
    UndoCommand cmd;
    cmd.description = "Delete Objects";
    cmd.undo = [this, removedObjects]() {
        for (const auto& pair : removedObjects) {
            if (pair.first && pair.second) {
                addPlacedObject(pair.first, pair.second);
            }
        }
    };
    cmd.redo = [this, removedObjects]() {
        for (const auto& pair : removedObjects) {
            if (pair.first && pair.second) {
                removePlacedObject(pair.first, pair.second);
            }
        }
    };
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<std::pair<int, int>>& moves) {
    if (objects.empty() || moves.empty() || objects.size() != moves.size()) {
        spdlog::warn("registerObjectMove: Invalid input - objects:{} moves:{}", objects.size(), moves.size());
        return false;
    }

    UndoCommand cmd;
    cmd.description = "Move Objects";
    cmd.undo = [this, objects, moves]() {
        if (_hexgrid.empty()) {
            spdlog::error("registerObjectMove undo: hex grid not available");
            return;
        }
        for (size_t i = 0; i < objects.size() && i < moves.size(); ++i) {
            auto object = objects[i];
            if (!object || !object->hasMapObject()) {
                continue;
            }

            auto& mapObj = object->getMapObject();
            mapObj.position = moves[i].first;
            if (auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(moves[i].first)); hex.has_value()) {
                object->setHexPosition(hex->get());
            } else {
                spdlog::warn("registerObjectMove undo: Invalid hex position {}", moves[i].first);
            }
        }
        _refreshObjects();
    };
    cmd.redo = [this, objects, moves]() {
        if (_hexgrid.empty()) {
            spdlog::error("registerObjectMove redo: hex grid not available");
            return;
        }
        for (size_t i = 0; i < objects.size() && i < moves.size(); ++i) {
            auto object = objects[i];
            if (!object || !object->hasMapObject()) {
                continue;
            }

            auto& mapObj = object->getMapObject();
            mapObj.position = moves[i].second;
            if (auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(moves[i].second)); hex.has_value()) {
                object->setHexPosition(hex->get());
            } else {
                spdlog::warn("registerObjectMove redo: Invalid hex position {}", moves[i].second);
            }
        }
        _refreshObjects();
    };

    cmd.redo();
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::registerObjectRotation(const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<int>& beforeDirs,
    const std::vector<int>& afterDirs) {
    if (objects.empty() || beforeDirs.size() != objects.size() || afterDirs.size() != objects.size()) {
        spdlog::warn("registerObjectRotation: Invalid input sizes - objects:{} before:{} after:{}",
            objects.size(), beforeDirs.size(), afterDirs.size());
        return false;
    }

    UndoCommand cmd;
    cmd.description = "Rotate Objects";
    cmd.undo = [objects, beforeDirs]() {
        for (size_t i = 0; i < objects.size() && i < beforeDirs.size(); ++i) {
            if (!objects[i]) {
                continue;
            }
            if (beforeDirs[i] < 0 || beforeDirs[i] > 5) {
                spdlog::warn("registerObjectRotation undo: Invalid direction {}", beforeDirs[i]);
                continue;
            }
            objects[i]->setDirection(static_cast<ObjectDirection>(beforeDirs[i]));
        }
    };
    cmd.redo = [objects, afterDirs]() {
        for (size_t i = 0; i < objects.size() && i < afterDirs.size(); ++i) {
            if (!objects[i]) {
                continue;
            }
            if (afterDirs[i] < 0 || afterDirs[i] > 5) {
                spdlog::warn("registerObjectRotation redo: Invalid direction {}", afterDirs[i]);
                continue;
            }
            objects[i]->setDirection(static_cast<ObjectDirection>(afterDirs[i]));
        }
    };

    return pushCommand(std::move(cmd));
}

void ObjectCommandController::applyFrmToObject(const std::shared_ptr<Object>& object, uint32_t frmPid, const std::string& frmPath) {
    if (!object) {
        spdlog::warn("applyFrmToObject: null object provided");
        return;
    }
    if (frmPath.empty()) {
        spdlog::warn("applyFrmToObject: empty FRM path provided");
        return;
    }

    try {
        const auto& texture = _resources.textures().get(frmPath);
        sf::Sprite sprite(texture);
        sprite.setPosition(object->getSprite().getPosition());
        object->setSprite(std::move(sprite));
        if (object->hasMapObject()) {
            object->getMapObject().frm_pid = frmPid;
            object->setDirection(static_cast<ObjectDirection>(object->getMapObject().direction));
        }
    } catch (const std::exception& e) {
        spdlog::error("ObjectCommandController::applyFrmToObject - failed to apply FRM {}: {}", frmPath, e.what());
    }
}

bool ObjectCommandController::registerObjectFrmChange(const std::shared_ptr<Object>& object,
    uint32_t oldFrmPid,
    const std::string& oldFrmPath,
    uint32_t newFrmPid,
    const std::string& newFrmPath) {
    if (!object) {
        return false;
    }
    if (newFrmPath.empty()) {
        spdlog::warn("registerObjectFrmChange: new FRM path empty, ignoring");
        return false;
    }

    UndoCommand cmd;
    cmd.description = "Change Object FRM";
    cmd.undo = [this, object, oldFrmPid, oldFrmPath]() {
        applyFrmToObject(object, oldFrmPid, oldFrmPath);
    };
    cmd.redo = [this, object, newFrmPid, newFrmPath]() {
        applyFrmToObject(object, newFrmPid, newFrmPath);
    };

    cmd.redo();
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::registerExitGridCreation(const std::vector<std::shared_ptr<MapObject>>& exitGrids, int elevation) {
    if (exitGrids.empty()) {
        return false;
    }

    UndoCommand cmd;
    cmd.description = exitGrids.size() == 1 ? "Place Exit Grid" : "Place Exit Grids";
    cmd.undo = [this, exitGrids, elevation]() {
        if (!_map) {
            return;
        }

        auto& mapFile = _map->getMapFile();
        if (mapFile.map_objects.find(elevation) == mapFile.map_objects.end()) {
            return;
        }

        auto& elevationObjects = mapFile.map_objects[elevation];
        for (const auto& exitGrid : exitGrids) {
            elevationObjects.erase(
                std::remove(elevationObjects.begin(), elevationObjects.end(), exitGrid),
                elevationObjects.end());
        }
        _refreshObjects();
    };
    cmd.redo = [this, exitGrids, elevation]() {
        if (!_map) {
            return;
        }

        auto& mapFile = _map->getMapFile();
        for (const auto& exitGrid : exitGrids) {
            mapFile.map_objects[elevation].push_back(exitGrid);
        }
        _refreshObjects();
    };

    cmd.redo();
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::registerExitGridEdit(const std::vector<std::shared_ptr<MapObject>>& exitGrids,
    const std::vector<ExitGridCommandState>& beforeStates,
    const std::vector<ExitGridCommandState>& afterStates) {
    if (exitGrids.empty() || exitGrids.size() != beforeStates.size() || exitGrids.size() != afterStates.size()) {
        spdlog::warn("registerExitGridEdit: Invalid input sizes - grids:{} before:{} after:{}",
            exitGrids.size(), beforeStates.size(), afterStates.size());
        return false;
    }

    UndoCommand cmd;
    cmd.description = exitGrids.size() == 1 ? "Edit Exit Grid" : "Edit Exit Grids";
    cmd.undo = [this, exitGrids, beforeStates]() {
        for (size_t i = 0; i < exitGrids.size(); ++i) {
            if (!exitGrids[i]) {
                continue;
            }
            exitGrids[i]->exit_map = beforeStates[i].exitMap;
            exitGrids[i]->exit_position = beforeStates[i].exitPosition;
            exitGrids[i]->exit_elevation = beforeStates[i].exitElevation;
            exitGrids[i]->exit_orientation = beforeStates[i].exitOrientation;
            exitGrids[i]->frm_pid = beforeStates[i].frmPid;
            exitGrids[i]->pro_pid = beforeStates[i].proPid;
        }
        _refreshObjects();
    };
    cmd.redo = [this, exitGrids, afterStates]() {
        for (size_t i = 0; i < exitGrids.size(); ++i) {
            if (!exitGrids[i]) {
                continue;
            }
            exitGrids[i]->exit_map = afterStates[i].exitMap;
            exitGrids[i]->exit_position = afterStates[i].exitPosition;
            exitGrids[i]->exit_elevation = afterStates[i].exitElevation;
            exitGrids[i]->exit_orientation = afterStates[i].exitOrientation;
            exitGrids[i]->frm_pid = afterStates[i].frmPid;
            exitGrids[i]->pro_pid = afterStates[i].proPid;
        }
        _refreshObjects();
    };

    return pushCommand(std::move(cmd));
}

MapObjectInstanceState ObjectCommandController::captureInstanceState(const MapObject& o) {
    MapObjectInstanceState s;
    s.flags = o.flags;
    s.dataFlags = o.unknown11;
    s.lightRadius = o.light_radius;
    s.lightIntensity = o.light_intensity;
    s.walkthrough = o.walkthrough;
    s.map = o.map;
    s.elevhex = o.elevhex;
    s.elevtype = o.elevtype;
    s.elevlevel = o.elevlevel;
    s.aiPacket = o.ai_packet;
    s.groupId = o.group_id;
    s.currentHp = o.current_hp;
    s.currentRad = o.current_rad;
    s.currentPoison = o.current_poison;
    return s;
}

void ObjectCommandController::applyInstanceState(MapObject& o, const MapObjectInstanceState& s) {
    o.flags = s.flags;
    o.unknown11 = s.dataFlags;
    o.light_radius = s.lightRadius;
    o.light_intensity = s.lightIntensity;
    o.walkthrough = s.walkthrough;
    o.map = s.map;
    o.elevhex = s.elevhex;
    o.elevtype = s.elevtype;
    o.elevlevel = s.elevlevel;
    o.ai_packet = s.aiPacket;
    o.group_id = s.groupId;
    o.current_hp = s.currentHp;
    o.current_rad = s.currentRad;
    o.current_poison = s.currentPoison;
}

bool ObjectCommandController::registerInstanceEdit(const std::shared_ptr<MapObject>& mapObject,
    const MapObjectInstanceState& before,
    const MapObjectInstanceState& after,
    const std::string& description) {
    if (!mapObject) {
        spdlog::warn("registerInstanceEdit: null mapObject");
        return false;
    }

    UndoCommand cmd;
    cmd.description = description;
    cmd.undo = [this, mapObject, before]() {
        applyInstanceState(*mapObject, before);
        _refreshObjects();
    };
    cmd.redo = [this, mapObject, after]() {
        applyInstanceState(*mapObject, after);
        _refreshObjects();
    };

    cmd.redo();
    return pushCommand(std::move(cmd));
}

std::vector<std::shared_ptr<MapObject>> ObjectCommandController::cloneInventory(
    const std::vector<std::unique_ptr<MapObject>>& inventory) {
    std::vector<std::shared_ptr<MapObject>> out;
    out.reserve(inventory.size());
    for (const auto& item : inventory) {
        if (item) {
            out.push_back(std::shared_ptr<MapObject>(item->cloneDeep()));
        }
    }
    return out;
}

namespace {
    void restoreInventory(MapObject& container, const std::vector<std::shared_ptr<MapObject>>& snapshot) {
        container.inventory.clear();
        container.inventory.reserve(snapshot.size());
        for (const auto& item : snapshot) {
            if (item) {
                container.inventory.push_back(item->cloneDeep());
            }
        }
        container.objects_in_inventory = static_cast<uint32_t>(container.inventory.size());
    }
} // namespace

bool ObjectCommandController::registerInventoryEdit(const std::shared_ptr<MapObject>& container,
    std::vector<std::shared_ptr<MapObject>> before,
    std::vector<std::shared_ptr<MapObject>> after) {
    if (!container) {
        return false;
    }

    UndoCommand cmd;
    cmd.description = "Edit Inventory";
    cmd.undo = [container, before]() { restoreInventory(*container, before); };
    cmd.redo = [container, after]() { restoreInventory(*container, after); };
    // The caller already applied the edit, so do not run redo() here.
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::clearElevationObjects(int elevation) {
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
    ScriptSections beforeScripts = snapshotScripts();
    it->second.clear();
    for (const auto& obj : before) {
        if (obj && obj->map_scripts_pid != -1) {
            eraseScript(static_cast<uint32_t>(obj->map_scripts_pid));
        }
    }
    ScriptSections afterScripts = snapshotScripts();

    UndoCommand cmd;
    cmd.description = "Clear Elevation Objects";
    cmd.undo = [this, elevation, before, beforeScripts]() {
        _map->getMapFile().map_objects[elevation] = before;
        restoreScripts(beforeScripts);
        _refreshObjects();
    };
    cmd.redo = [this, elevation, after, afterScripts]() {
        _map->getMapFile().map_objects[elevation] = after;
        restoreScripts(afterScripts);
        _refreshObjects();
    };
    // The edit was already applied above; only register the undo/redo handlers.
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::copyElevation(int fromElevation, int toElevation) {
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
    return pushCommand(std::move(cmd));
}

uint32_t ObjectCommandController::allocateScriptId(int section) const {
    if (!_map || section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return 0;
    }
    uint32_t nextId = 0;
    for (const auto& s : _map->getMapFile().map_scripts[section]) {
        nextId = std::max(nextId, MapScript::sidIndex(s.pid) + 1);
    }
    return nextId;
}

uint32_t ObjectCommandController::allocateObjectId() const {
    if (!_map) {
        return 1;
    }
    uint32_t maxId = 0;
    const auto& mapFile = _map->getMapFile();
    for (const auto& [elevation, objects] : mapFile.map_objects) {
        for (const auto& obj : objects) {
            if (obj) {
                maxId = std::max(maxId, obj->unknown0);
            }
        }
    }
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        for (const auto& s : mapFile.map_scripts[section]) {
            maxId = std::max(maxId, s.script_oid);
        }
    }
    return maxId + 1;
}

ObjectCommandController::ScriptSections ObjectCommandController::snapshotScripts() const {
    static_assert(SCRIPT_SECTIONS == Map::SCRIPT_SECTIONS,
        "ScriptSections snapshot size must match Map::SCRIPT_SECTIONS");
    ScriptSections snapshot;
    const auto& mapFile = _map->getMapFile();
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        snapshot.sections[section] = mapFile.map_scripts[section];
        snapshot.counts[section] = mapFile.scripts_in_section[section];
    }
    return snapshot;
}

void ObjectCommandController::restoreScripts(const ScriptSections& snapshot) {
    auto& mapFile = _map->getMapFile();
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        mapFile.map_scripts[section] = snapshot.sections[section];
        mapFile.scripts_in_section[section] = snapshot.counts[section];
    }
}

void ObjectCommandController::eraseScript(uint32_t sid) {
    const int section = MapScript::sidSection(sid);
    if (section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return;
    }
    auto& vec = _map->getMapFile().map_scripts[section];
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                  [sid](const MapScript& s) { return s.pid == sid; }),
        vec.end());
    _map->getMapFile().scripts_in_section[section] = static_cast<int>(vec.size());
}

void ObjectCommandController::removeObjectScript(MapObject& object) {
    if (!_map || object.map_scripts_pid == -1) {
        return;
    }
    eraseScript(static_cast<uint32_t>(object.map_scripts_pid));
    object.map_scripts_pid = -1;
}

void ObjectCommandController::applyScriptSnapshot(int section, const std::shared_ptr<MapObject>& object,
    const std::vector<MapScript>& sectionScripts, uint32_t oid, int32_t sid) {
    if (!_map || section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return;
    }
    auto& mapFile = _map->getMapFile();
    mapFile.map_scripts[section] = sectionScripts;
    mapFile.scripts_in_section[section] = static_cast<int>(sectionScripts.size());
    if (object) {
        object->unknown0 = oid;
        object->map_scripts_pid = sid;
    }
}

bool ObjectCommandController::recordScriptEdit(const std::string& description, int section,
    const std::shared_ptr<MapObject>& object,
    std::vector<MapScript> beforeSection, uint32_t beforeOid, int32_t beforeSid) {
    if (!_map) {
        return false;
    }
    // The caller already applied the edit; capture the resulting "after" state.
    std::vector<MapScript> afterSection = _map->getMapFile().map_scripts[section];
    uint32_t afterOid = object ? object->unknown0 : 0;
    int32_t afterSid = object ? object->map_scripts_pid : -1;

    UndoCommand cmd;
    cmd.description = description;
    cmd.undo = [this, section, object, beforeSection, beforeOid, beforeSid]() {
        applyScriptSnapshot(section, object, beforeSection, beforeOid, beforeSid);
    };
    cmd.redo = [this, section, object, afterSection, afterOid, afterSid]() {
        applyScriptSnapshot(section, object, afterSection, afterOid, afterSid);
    };
    return pushCommand(std::move(cmd));
}

bool ObjectCommandController::attachScript(const std::shared_ptr<MapObject>& object,
    int scriptType, uint32_t programIndex) {
    if (!_map || !object || scriptType < 0 || scriptType >= Map::SCRIPT_SECTIONS) {
        return false;
    }
    const int section = scriptType;
    auto& mapFile = _map->getMapFile();

    std::vector<MapScript> beforeSection = mapFile.map_scripts[section];
    const uint32_t beforeOid = object->unknown0;
    const int32_t beforeSid = object->map_scripts_pid;

    // Replace any existing script (same object type -> same section).
    removeObjectScript(*object);

    const uint32_t scriptId = allocateScriptId(section);
    const uint32_t oid = allocateObjectId();
    MapScript script = MapScript::makeObjectScript(static_cast<MapScript::ScriptType>(scriptType),
        scriptId, programIndex, oid);
    mapFile.map_scripts[section].push_back(script);
    mapFile.scripts_in_section[section] = static_cast<int>(mapFile.map_scripts[section].size());
    object->unknown0 = oid;
    object->map_scripts_pid = static_cast<int32_t>(script.pid);

    return recordScriptEdit("Attach Script", section, object, std::move(beforeSection), beforeOid, beforeSid);
}

bool ObjectCommandController::detachScript(const std::shared_ptr<MapObject>& object) {
    if (!_map || !object || object->map_scripts_pid == -1) {
        return false;
    }
    const int section = MapScript::sidSection(static_cast<uint32_t>(object->map_scripts_pid));
    if (section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return false;
    }

    std::vector<MapScript> beforeSection = _map->getMapFile().map_scripts[section];
    const uint32_t beforeOid = object->unknown0;
    const int32_t beforeSid = object->map_scripts_pid;

    removeObjectScript(*object);

    return recordScriptEdit("Detach Script", section, object, std::move(beforeSection), beforeOid, beforeSid);
}

bool ObjectCommandController::addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius) {
    if (!_map) {
        return false;
    }
    const int section = static_cast<int>(MapScript::ScriptType::SPATIAL);
    auto& mapFile = _map->getMapFile();

    std::vector<MapScript> beforeSection = mapFile.map_scripts[section];

    const uint32_t scriptId = allocateScriptId(section);
    const uint32_t oid = allocateObjectId();
    MapScript script = MapScript::makeSpatialScript(scriptId, programIndex,
        static_cast<uint32_t>(tile), static_cast<uint32_t>(elevation), static_cast<uint32_t>(radius), oid);
    mapFile.map_scripts[section].push_back(script);
    mapFile.scripts_in_section[section] = static_cast<int>(mapFile.map_scripts[section].size());

    return recordScriptEdit("Add Spatial Script", section, nullptr, std::move(beforeSection), 0, -1);
}

bool ObjectCommandController::pushCommand(UndoCommand cmd) {
    _undoStack.push(std::move(cmd));
    if (_onStackChanged) {
        _onStackChanged();
    }
    return true;
}

} // namespace geck
