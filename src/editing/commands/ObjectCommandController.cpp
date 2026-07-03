#include "ObjectCommandController.h"

#include "editing/commands/CommandHost.h"

// ObjectCommandController is a thin facade: its constructor wires up the editing
// services and every method delegates to one of them, so only the service/param
// types (forward-declared via the header) are needed here. The host's hooks are
// adapted to the services' std::function parameters by the bridging lambdas below;
// each captures `this` and forwards to _host, which the caller guarantees outlives us.

namespace geck {

ObjectCommandController::ObjectCommandController(resource::GameResources& resources,
    std::unique_ptr<Map>& map,
    const HexagonGrid& hexgrid,
    MapSpriteLoader& mapSpriteLoader,
    std::vector<std::shared_ptr<Object>>& objects,
    std::vector<sf::Sprite>& wallBlockerOverlays,
    UndoStack& undoStack,
    CommandHost& host)
    : _resources(resources)
    , _map(map)
    , _hexgrid(hexgrid)
    , _mapSpriteLoader(mapSpriteLoader)
    , _objects(objects)
    , _wallBlockerOverlays(wallBlockerOverlays)
    , _host(host)
    , _batcher(undoStack, [this] { _host.undoStackChanged(); })
    , _tileService(map, _batcher, [this](int elevation) -> std::vector<Tile>& { return _host.ensureElevationTiles(elevation); }, [this] { return _host.getCurrentElevation(); }, [this](int hexIndex, bool isRoof, int elevation) { _host.updateTileSprite(hexIndex, isRoof, elevation); })
    , _inventoryService(_batcher)
    , _scriptService(map, _batcher)
    , _refreshObjects([this] { _host.refreshObjects(); })
    , _reloadTiles([this] { _host.reloadTiles(); })
    , _mapService(map, objects, wallBlockerOverlays, _scriptService, _batcher, _refreshObjects, _reloadTiles)
    , _objectService(resources, map, hexgrid, mapSpriteLoader, objects, wallBlockerOverlays, _batcher, _refreshObjects) {
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
    _objectService.addObjectData(mapObject);
}

void ObjectCommandController::removeObjectData(const std::shared_ptr<MapObject>& mapObject) {
    _objectService.removeObjectData(mapObject);
}

void ObjectCommandController::addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _objectService.addPlacedObject(mapObject, object);
}

void ObjectCommandController::removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _objectService.removePlacedObject(mapObject, object);
}

bool ObjectCommandController::registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    return _objectService.registerObjectPlacement(mapObject, object);
}

bool ObjectCommandController::registerObjectData(const std::shared_ptr<MapObject>& mapObject) {
    return _objectService.registerObjectData(mapObject);
}

bool ObjectCommandController::registerObjectDeletion(
    const std::vector<std::pair<std::shared_ptr<MapObject>, std::shared_ptr<Object>>>& removedObjects) {
    return _objectService.registerObjectDeletion(removedObjects);
}

bool ObjectCommandController::registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<std::pair<int, int>>& moves) {
    return _objectService.registerObjectMove(objects, moves);
}

bool ObjectCommandController::registerObjectRotation(const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<int>& beforeDirs,
    const std::vector<int>& afterDirs) {
    return _objectService.registerObjectRotation(objects, beforeDirs, afterDirs);
}

void ObjectCommandController::applyFrmToObject(const std::shared_ptr<Object>& object, uint32_t frmPid, const std::string& frmPath) {
    _objectService.applyFrmToObject(object, frmPid, frmPath);
}

bool ObjectCommandController::registerObjectFrmChange(const std::shared_ptr<Object>& object,
    uint32_t oldFrmPid,
    const std::string& oldFrmPath,
    uint32_t newFrmPid,
    const std::string& newFrmPath) {
    return _objectService.registerObjectFrmChange(object, oldFrmPid, oldFrmPath, newFrmPid, newFrmPath);
}

bool ObjectCommandController::registerExitGridCreation(const std::vector<std::shared_ptr<MapObject>>& exitGrids, int elevation) {
    return _objectService.registerExitGridCreation(exitGrids, elevation);
}

bool ObjectCommandController::registerExitGridEdit(const std::vector<std::shared_ptr<MapObject>>& exitGrids,
    const std::vector<ExitGridCommandState>& beforeStates,
    const std::vector<ExitGridCommandState>& afterStates) {
    return _objectService.registerExitGridEdit(exitGrids, beforeStates, afterStates);
}

MapObjectInstanceState ObjectCommandController::captureInstanceState(const MapObject& object) {
    return ObjectEditService::captureInstanceState(object);
}

bool ObjectCommandController::registerInstanceEdit(const std::shared_ptr<MapObject>& mapObject,
    const MapObjectInstanceState& before,
    const MapObjectInstanceState& after,
    const std::string& description) {
    return _objectService.registerInstanceEdit(mapObject, before, after, description);
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

bool ObjectCommandController::editSpatialScript(uint32_t sid, uint32_t programIndex, int tile, int elevation, int radius) {
    return _scriptService.editSpatialScript(sid, programIndex, tile, elevation, radius);
}

bool ObjectCommandController::removeSpatialScript(uint32_t sid) {
    return _scriptService.removeSpatialScript(sid);
}

const MapScript* ObjectCommandController::findSpatialScript(uint32_t sid) const {
    return _scriptService.findSpatialScript(sid);
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
