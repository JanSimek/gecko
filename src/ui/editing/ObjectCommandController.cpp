#include "ObjectCommandController.h"

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "resource/GameResources.h"
#include "editor/TileChange.h"
#include "util/UndoStack.h"
#include "ui/rendering/MapSpriteLoader.h"

#include <algorithm>
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
    , _batcher(undoStack, std::move(onStackChanged))
    , _tileService(map, _batcher, std::move(ensureElevationTiles), std::move(getCurrentElevation), std::move(updateTileSprite))
    , _inventoryService(_batcher)
    , _scriptService(map, _batcher)
    , _refreshObjects(std::move(refreshObjects))
    , _reloadTiles(std::move(reloadTiles))
    , _mapService(map, objects, wallBlockerOverlays, _scriptService, _batcher, _refreshObjects, _reloadTiles) {
}

void ObjectCommandController::applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState) {
    _tileService.applyTileChanges(changes, applyAfterState);
}

void ObjectCommandController::registerTileEdit(const std::string& description, const std::vector<TileChange>& changes) {
    _tileService.registerTileEdit(description, changes);
}

void ObjectCommandController::applyTileEdit(const std::string& description, const std::vector<TileChange>& changes) {
    _tileService.applyTileEdit(description, changes);
}

void ObjectCommandController::addObjectData(const std::shared_ptr<MapObject>& mapObject) {
    if (!mapObject || !_map) {
        spdlog::warn("addObjectData: Invalid parameters - mapObject:{} map:{}",
            mapObject != nullptr, _map != nullptr);
        return;
    }
    if (mapObject->elevation >= 3) {
        spdlog::error("addObjectData: Invalid elevation {}", mapObject->elevation);
        return;
    }
    _map->getMapFile().map_objects[mapObject->elevation].push_back(mapObject);
}

void ObjectCommandController::removeObjectData(const std::shared_ptr<MapObject>& mapObject) {
    if (!mapObject || !_map) {
        spdlog::warn("removeObjectData: Invalid parameters - mapObject:{} map:{}",
            mapObject != nullptr, _map != nullptr);
        return;
    }
    if (mapObject->elevation >= 3) {
        spdlog::error("removeObjectData: Invalid elevation {}", mapObject->elevation);
        return;
    }
    auto& elevationObjects = _map->getMapFile().map_objects[mapObject->elevation];
    std::erase(elevationObjects, mapObject);
    _refreshObjects();
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

bool ObjectCommandController::registerObjectData(const std::shared_ptr<MapObject>& mapObject) {
    if (!mapObject) {
        spdlog::warn("registerObjectData: null mapObject");
        return false;
    }

    UndoCommand cmd;
    cmd.description = "Place Object";
    cmd.redo = [this, mapObject]() {
        if (mapObject) {
            addObjectData(mapObject);
        }
    };
    cmd.undo = [this, mapObject]() {
        if (mapObject) {
            removeObjectData(mapObject);
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
    return InventoryEditService::cloneInventory(inventory);
}

bool ObjectCommandController::registerInventoryEdit(const std::shared_ptr<MapObject>& container,
    std::vector<std::shared_ptr<MapObject>> before,
    std::vector<std::shared_ptr<MapObject>> after) {
    return _inventoryService.registerInventoryEdit(container, std::move(before), std::move(after));
}

void ObjectCommandController::newEmptyMap() {
    _mapService.newEmptyMap();
}

bool ObjectCommandController::clearElevationObjects(int elevation) {
    return _mapService.clearElevationObjects(elevation);
}

bool ObjectCommandController::copyElevation(int fromElevation, int toElevation) {
    return _mapService.copyElevation(fromElevation, toElevation);
}

bool ObjectCommandController::attachScript(const std::shared_ptr<MapObject>& object, int scriptType, uint32_t programIndex) {
    return _scriptService.attachScript(object, scriptType, programIndex);
}

bool ObjectCommandController::detachScript(const std::shared_ptr<MapObject>& object) {
    return _scriptService.detachScript(object);
}

bool ObjectCommandController::addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius) {
    return _scriptService.addSpatialScript(programIndex, tile, elevation, radius);
}

bool ObjectCommandController::pushCommand(UndoCommand cmd) {
    return _batcher.push(std::move(cmd));
}

void ObjectCommandController::beginBatch(const std::string& description) {
    _batcher.beginBatch(description);
}

bool ObjectCommandController::endBatch() {
    return _batcher.endBatch();
}

} // namespace geck
