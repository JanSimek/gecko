#pragma once

#include <cstdint>

#include "support/ByteWriter.h"

// Proto records laid out by hand in the order fallout2-ce protoRead and its per-type readers
// (proto.cc) consume them, so a test pins the engine's layout instead of gecko's reader and writer
// agreeing with each other.
namespace geck::test {

/// The item header: pid, messageId, fid, lightDistance, lightIntensity, flags, extendedFlags, sid,
/// type, material (1, Metal), size, weight 7, cost 250, inventoryFid, then a one-byte soundId.
inline ByteWriter itemProtoHeader(int32_t pid, int32_t messageId, uint32_t extendedFlags, int32_t type) {
    ByteWriter w;
    w.be32s({ pid, messageId, 0x07000010, 0, 0, 0x00000008 });
    w.be32(extendedFlags);
    w.be32s({ -1, type, 1, 3, 7, 250, 0x07000020 });
    w.u8('0');
    return w;
}

/// A whole 122-byte weapon record: animationCode 6, damage 10-20, damageType 1 (laser), range 25/20,
/// no projectile, minStrength 4, AP cost 5/6, criticalFail 2, perk 58 (Weapon Long Range),
/// burstRounds 10, caliber 8 (10mm), ammoPid 3, ammoCapacity 30, soundId 'A'.
inline ByteWriter weaponProto(int32_t pid, int32_t messageId, uint32_t extendedFlags) {
    ByteWriter w = itemProtoHeader(pid, messageId, extendedFlags, 3 /* ITEM_TYPE_WEAPON */);
    w.be32s({ 6, 10, 20, 1, 25, 20, -1, 4, 5, 6, 2, 58, 10, 8, 3, 30 });
    w.u8('A');
    return w;
}

/// A whole 416-byte critter record, pid 0x01000001 / messageId 200, with the flags word Barter +
/// CRITTER_FLAT. Base stat i holds `baseSeed` + i, bonus stat i `bonusSeed` + i and skill i
/// `skillSeed` + i, so every reported figure names the slots it came from; then bodyType 1
/// (quadruped), experience 150, killType 3 and damageType 2 (fire).
inline ByteWriter critterProto(int32_t baseSeed, int32_t bonusSeed, int32_t skillSeed) {
    ByteWriter w;
    w.be32s({ 0x01000001, 200, 0x01000040, 0, 0, 0x20000000, 0x6000, 0x04000005, -1, 77, 1 });
    w.be32(0x0802);
    for (int32_t stat = 0; stat < 35; ++stat) {
        w.be32(static_cast<uint32_t>(baseSeed + stat));
    }
    for (int32_t stat = 0; stat < 35; ++stat) {
        w.be32(static_cast<uint32_t>(bonusSeed + stat));
    }
    for (int32_t skill = 0; skill < 18; ++skill) {
        w.be32(static_cast<uint32_t>(skillSeed + skill));
    }
    w.be32s({ 1, 150, 3, 2 });
    return w;
}

} // namespace geck::test
