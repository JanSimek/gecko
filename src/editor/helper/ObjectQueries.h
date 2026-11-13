#pragma once

#include <cstdint>

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

    /// True when the object's PRO carries the ShootThrough flag.
    bool isShootThroughWallBlocker(const MapObject& object, resource::GameResources& resources);

    /// Seed a new object's flags and light from its PRO (MapObject::applyProtoDefaults), as the
    /// engine does on creation. An unloadable PRO leaves them untouched, as the engine's obj_new
    /// does; returns whether the PRO loaded.
    bool applyProtoDefaults(MapObject& object, resource::GameResources& resources);

    /// The script SID the object's PRO gives a new instance (Pro::sid()), or -1 for none or an
    /// unloadable PRO.
    int32_t protoSid(const MapObject& object, resource::GameResources& resources);

} // namespace object_query

} // namespace geck
