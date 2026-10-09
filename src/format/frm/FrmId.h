#pragma once

#include <cstdint>

#include "format/ObjTypes.h"

namespace geck {

// Mirrors fallout2-ce art.h / art_defs.h: the FrmId class and the *FrameId enums, by the engine's
// names, keeping only the entries gecko refers to.

enum class MiscFrameId : int {
    Reserved = 0,       // reserved.frm
    Block = 1,          // block.frm - the art the shipped Scroll Blocker proto (0x0500000C) points at
    ScrollBlocker = 12, // scrblk.frm
};

enum class InterfaceFrameId : int {
    First = 0,
    ExitGridMarker = 3, // msef001.frm - exit grid marker
};

template <typename T>
struct FrameIdObjectType;

template <>
struct FrameIdObjectType<MiscFrameId> {
    static constexpr ObjectType value = ObjectType::Misc;
};

template <>
struct FrameIdObjectType<InterfaceFrameId> {
    static constexpr ObjectType value = ObjectType::Interface;
};

template <typename T>
concept FrameIdEnum = requires { FrameIdObjectType<T>::value; };

/// An art FID. Layout (engine art.h "FID Structure"):
///   bits 28-30 rotation, 24-27 object type, 16-23 animation type,
///   12-15 weapon animation, 0-11 frame id (the art's .lst index).
///
/// Mirrors the engine's FrmId accessor names, but holds the raw value losslessly and does no file
/// system lookups - resolving a FID to a path is FrmResolver's job.
class FrmId {
public:
    static constexpr uint32_t EMPTY_FID = 0xFFFFFFFFu; // engine kEmptyFid (-1)
    static constexpr uint32_t MAX_FRAME_ID = 0x00000FFFu;

    constexpr FrmId() = default;

    constexpr explicit FrmId(uint32_t fid)
        : _fid(fid) {
    }

    /// Engine buildFid. animationType/weaponAnimation are the raw codes (the engine's
    /// AnimationType / WeaponAnimation, which gecko does not model).
    constexpr FrmId(ObjectType type, uint32_t frameId, uint32_t animationType = 0, uint32_t weaponAnimation = 0,
        Rotation rotation = Rotation::NE)
        : _fid(((static_cast<uint32_t>(rotation) << ROTATION_SHIFT) & ROTATION_MASK)
              | ((static_cast<uint32_t>(type) << TYPE_SHIFT) & TYPE_MASK)
              | ((animationType << ANIMATION_TYPE_SHIFT) & ANIMATION_TYPE_MASK)
              | ((weaponAnimation << WEAPON_ANIMATION_SHIFT) & WEAPON_ANIMATION_MASK)
              | (frameId & MAX_FRAME_ID)) {
    }

    template <FrameIdEnum T>
    constexpr FrmId(T frameId)
        : FrmId(FrameIdObjectType<T>::value, static_cast<uint32_t>(frameId)) {
    }

    /// The raw FID, as stored in .map and .pro files.
    constexpr uint32_t fid() const { return _fid; }

    /// The type nibble (engine FID_TYPE, `(fid & 0x0F000000) >> 24`; the bits above it are the
    /// rotation), or ObjectType::Invalid when it names no art type.
    constexpr ObjectType objectType() const {
        const int type = static_cast<int>((_fid & TYPE_MASK) >> TYPE_SHIFT);
        return objectTypeIsValid(type) ? static_cast<ObjectType>(type) : ObjectType::Invalid;
    }

    /// The art's index in its type's .lst.
    constexpr uint32_t frameId() const { return _fid & MAX_FRAME_ID; }

    constexpr uint32_t animationType() const { return (_fid & ANIMATION_TYPE_MASK) >> ANIMATION_TYPE_SHIFT; }
    constexpr uint32_t weaponAnimation() const { return (_fid & WEAPON_ANIMATION_MASK) >> WEAPON_ANIMATION_SHIFT; }

    /// The rotation bits, or Rotation::Invalid when they name no facing. For critters NE (0)
    /// selects the multi-direction .frm and NE+n the single-direction .fr(n-1).
    constexpr Rotation rotation() const {
        const int rotation = static_cast<int>((_fid & ROTATION_MASK) >> ROTATION_SHIFT);
        return rotationIsValid(rotation) ? static_cast<Rotation>(rotation) : Rotation::Invalid;
    }

    constexpr bool hasFid() const { return static_cast<int32_t>(_fid) >= 0; }
    constexpr bool is(ObjectType type) const { return objectType() == type; }

    constexpr bool operator==(const FrmId&) const = default;

    template <FrameIdEnum T>
    constexpr bool operator==(T frameId) const {
        return *this == FrmId(frameId);
    }

private:
    static constexpr uint32_t WEAPON_ANIMATION_MASK = 0x0000F000u;
    static constexpr uint32_t ANIMATION_TYPE_MASK = 0x00FF0000u;
    static constexpr uint32_t TYPE_MASK = 0x0F000000u;
    static constexpr uint32_t ROTATION_MASK = 0x70000000u;

    static constexpr unsigned WEAPON_ANIMATION_SHIFT = 12;
    static constexpr unsigned ANIMATION_TYPE_SHIFT = 16;
    static constexpr unsigned TYPE_SHIFT = 24;
    static constexpr unsigned ROTATION_SHIFT = 28;

    uint32_t _fid = EMPTY_FID;
};

} // namespace geck
