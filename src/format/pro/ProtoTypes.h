#pragma once

#include <cstdint>

#include "format/ObjTypes.h"

namespace geck {

// Mirrors fallout2-ce proto_types.h. Enumerators drop the engine prefix (ITEM_TYPE_ARMOR ->
// ItemType::Armor); the *ProtoTypeId enums use the engine's names verbatim, keeping only the
// entries gecko refers to.

/// Engine ItemType (ITEM_TYPE_*): an item proto's subtype.
enum class ItemType : int {
    Armor = 0,
    Container,
    Drug,
    Weapon,
    Ammo,
    Misc,
    Key,
    Count,
};

/// Engine SceneryType (SCENERY_TYPE_*): a scenery proto's subtype.
enum class SceneryType : int {
    Door = 0,
    Stairs,
    Elevator,
    LadderUp,
    LadderDown,
    Generic,
    Count,
};

enum class ItemProtoTypeId : int {
    Reserved = 0,
    Money = 41,
};

enum class CritterProtoTypeId : int {
    Reserved = 0,
    Dude = Reserved,
};

enum class SceneryProtoTypeId : int {
    Reserved = 0,
};

enum class WallProtoTypeId : int {
    Reserved = 0,
};

enum class TileProtoTypeId : int {
    Reserved = 0,
};

enum class MiscProtoTypeId : int {
    Reserved = 0,
    // The engine still calls this Id0x0C; its only use is _obj_scroll_blocking_at() matching
    // `obj->pid == 0x500000C` (object.cc), and proto.msg names it "Scroll Blocker".
    ScrollBlocker = 12,
    FirstExitGrid = 16,
    LastExitGrid = 23,
};

template <typename T>
struct ProtoTypeIdObjectType;

template <>
struct ProtoTypeIdObjectType<ItemProtoTypeId> {
    static constexpr ObjectType value = ObjectType::Item;
};

template <>
struct ProtoTypeIdObjectType<CritterProtoTypeId> {
    static constexpr ObjectType value = ObjectType::Critter;
};

template <>
struct ProtoTypeIdObjectType<SceneryProtoTypeId> {
    static constexpr ObjectType value = ObjectType::Scenery;
};

template <>
struct ProtoTypeIdObjectType<WallProtoTypeId> {
    static constexpr ObjectType value = ObjectType::Wall;
};

template <>
struct ProtoTypeIdObjectType<TileProtoTypeId> {
    static constexpr ObjectType value = ObjectType::Tile;
};

template <>
struct ProtoTypeIdObjectType<MiscProtoTypeId> {
    static constexpr ObjectType value = ObjectType::Misc;
};

template <typename T>
concept ProtoTypeIdEnum = requires { ProtoTypeIdObjectType<T>::value; };

/// A proto PID: the object type in the high byte, the proto index in the low 24 bits.
///
/// Mirrors the engine's ProtoId (proto_types.h) with the same accessor names, but holds the raw
/// value losslessly: gecko round-trips PIDs the engine would reject (e.g. -1, see CLAUDE.md
/// "Objects With No Known Type"), so validity is a query here, never a normalisation.
class ProtoId {
public:
    static constexpr uint32_t EMPTY_PID = 0xFFFFFFFFu; // engine kEmptyPid (-1)
    static constexpr uint32_t MAX_PROTO_ID = 0x00FFFFFFu;

    constexpr ProtoId() = default;

    constexpr explicit ProtoId(uint32_t pid)
        : _pid(pid) {
    }

    constexpr ProtoId(ObjectType type, uint32_t protoId)
        : _pid((static_cast<uint32_t>(type) << TYPE_SHIFT) | (protoId & MAX_PROTO_ID)) {
    }

    template <ProtoTypeIdEnum T>
    constexpr ProtoId(T protoId)
        : ProtoId(ProtoTypeIdObjectType<T>::value, static_cast<uint32_t>(protoId)) {
    }

    /// The raw PID, as stored in .map and .pro files.
    constexpr uint32_t pid() const { return _pid; }

    /// The PID's high byte as a proto type, or ObjectType::Invalid when it names none (engine
    /// ProtoId::objectType, which reads the whole byte: `pid >> 24`).
    constexpr ObjectType objectType() const {
        const int type = static_cast<int>(_pid >> TYPE_SHIFT);
        return protoObjectTypeIsValid(type) ? static_cast<ObjectType>(type) : ObjectType::Invalid;
    }

    /// The proto index: the PID's low 24 bits.
    constexpr uint32_t protoId() const { return _pid & MAX_PROTO_ID; }

    constexpr bool hasPid() const { return static_cast<int32_t>(_pid) >= 0; }
    constexpr bool hasObjectType() const { return objectType() != ObjectType::Invalid; }
    constexpr bool valid() const { return hasPid() && hasObjectType(); }

    constexpr bool is(ObjectType type) const { return objectType() == type; }

    constexpr bool operator==(const ProtoId&) const = default;

    template <ProtoTypeIdEnum T>
    constexpr bool operator==(T protoId) const {
        return *this == ProtoId(protoId);
    }

private:
    static constexpr unsigned TYPE_SHIFT = 24;

    uint32_t _pid = EMPTY_PID;
};

} // namespace geck
