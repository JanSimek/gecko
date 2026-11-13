#pragma once

namespace geck {

// Mirrors fallout2-ce obj_types.h. The engine's unscoped enumerators map 1:1 to these scoped
// names by dropping the prefix (OBJ_TYPE_ITEM -> ObjectType::Item, ROTATION_NE -> Rotation::NE),
// so code can be compared side by side with the engine.

/// Engine ObjectType: the type nibble of a PID or FID. The first six are proto types; the rest
/// only name art (FID) folders.
enum class ObjectType : int {
    Invalid = -1,
    Item,
    Critter,
    Scenery,
    Wall,
    Tile,
    Misc,
    Interface,
    Inventory,
    Head,
    Background,
    SkillDex,
    Count,
    ProtoCount = Interface,
    First = Item,
};

/// Engine Rotation: the six hex facings, also the FID rotation bits.
enum class Rotation : int {
    Invalid = -1,
    NE,
    E,
    SE,
    SW,
    W,
    NW,
    Count,
    First = NE,
    Last = NW,
};

constexpr bool objectTypeIsValid(int type) {
    return type >= static_cast<int>(ObjectType::First) && type < static_cast<int>(ObjectType::Count);
}

constexpr bool protoObjectTypeIsValid(int type) {
    return type >= static_cast<int>(ObjectType::First) && type < static_cast<int>(ObjectType::ProtoCount);
}

constexpr bool rotationIsValid(int rotation) {
    return rotation >= static_cast<int>(Rotation::First) && rotation < static_cast<int>(Rotation::Count);
}

} // namespace geck
