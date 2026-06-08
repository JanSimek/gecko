#pragma once

namespace geck {

struct MapObject;
namespace resource {
    class GameResources;
}

/// Queries about a MapObject that require loading its PRO data, and therefore
/// the app-layer resource stack. These live outside MapObject (a vault POD) so
/// that vault stays free of any dependency on the resource layer.
namespace object_query {

    /// True when the object's PRO lacks the NoBlock flag (i.e. it blocks movement).
    bool blocksMovement(const MapObject& object, resource::GameResources& resources);

    /// True when the object acts as a wall blocker (currently == blocksMovement).
    bool isWallBlocker(const MapObject& object, resource::GameResources& resources);

    /// True when the object's PRO carries the ShootThrough flag.
    bool isShootThroughWallBlocker(const MapObject& object, resource::GameResources& resources);

} // namespace object_query

} // namespace geck
