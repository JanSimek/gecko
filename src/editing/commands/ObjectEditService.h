#pragma once

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace geck {

class HexagonGrid;
class Map;
class MapSpriteLoader;
class Object;
struct MapObject;
class UndoBatcher;

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

/**
 * @brief Object placement, movement, rotation, FRM, exit-grid, and per-instance
 *        editing — the largest aggregate ObjectCommandController delegates to.
 *
 * Built on the shared UndoBatcher. Holds references to the live object model and
 * its sprite caches so placements/deletions update both the map data and the
 * rendered view.
 */
class ObjectEditService {
public:
    ObjectEditService(resource::GameResources& resources,
        std::unique_ptr<Map>& map,
        const HexagonGrid& hexgrid,
        MapSpriteLoader& mapSpriteLoader,
        std::vector<std::shared_ptr<Object>>& objects,
        std::vector<sf::Sprite>& wallBlockerOverlays,
        UndoBatcher& batcher,
        std::function<void()> refreshObjects);

    void addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    void removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    void addObjectData(const std::shared_ptr<MapObject>& mapObject);
    void removeObjectData(const std::shared_ptr<MapObject>& mapObject);

    bool registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
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
    bool registerInstanceEdit(const std::shared_ptr<MapObject>& mapObject,
        const MapObjectInstanceState& before,
        const MapObjectInstanceState& after,
        const std::string& description);

private:
    static void applyInstanceState(MapObject& object, const MapObjectInstanceState& state);
    // Re-point each object to its target hex position / direction. Shared by the
    // undo and redo halves of registerObjectMove / registerObjectRotation.
    void applyObjectPositions(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<int>& positions);
    static void applyObjectDirections(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<int>& directions);
    // Applies one exit-grid state snapshot to an object (shared by the undo/redo halves).
    static void applyExitGridState(MapObject& exitGrid, const ExitGridCommandState& state);

    resource::GameResources& _resources;
    std::unique_ptr<Map>& _map;
    const HexagonGrid& _hexgrid;
    MapSpriteLoader& _mapSpriteLoader;
    std::vector<std::shared_ptr<Object>>& _objects;
    std::vector<sf::Sprite>& _wallBlockerOverlays;
    UndoBatcher& _batcher;
    std::function<void()> _refreshObjects;
};

} // namespace geck
