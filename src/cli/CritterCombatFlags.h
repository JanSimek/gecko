#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <utility>

#include <nlohmann/json.hpp>

namespace geck::cli {

/// Names for the bit fields in a critter's saved combat state, verbatim from fallout2-ce obj_types.h
/// (CritterManeuver and Dam). They are engine code, not game data, so they cannot be loaded from a
/// .msg; the engine's own spelling is kept, including CRITTER_MANUEVER_FLEEING.
inline constexpr std::array<std::pair<uint32_t, const char*>, 3> kCritterManeuverFlags{ {
    { 0x01, "CRITTER_MANEUVER_ENGAGING" },
    { 0x02, "CRITTER_MANEUVER_DISENGAGING" },
    { 0x04, "CRITTER_MANUEVER_FLEEING" },
} };

inline constexpr std::array<std::pair<uint32_t, const char*>, 24> kDamFlags{ {
    { 0x01, "DAM_KNOCKED_OUT" },
    { 0x02, "DAM_KNOCKED_DOWN" },
    { 0x04, "DAM_CRIP_LEG_LEFT" },
    { 0x08, "DAM_CRIP_LEG_RIGHT" },
    { 0x10, "DAM_CRIP_ARM_LEFT" },
    { 0x20, "DAM_CRIP_ARM_RIGHT" },
    { 0x40, "DAM_BLIND" },
    { 0x80, "DAM_DEAD" },
    { 0x100, "DAM_HIT" },
    { 0x200, "DAM_CRITICAL" },
    { 0x400, "DAM_ON_FIRE" },
    { 0x800, "DAM_BYPASS" },
    { 0x1000, "DAM_EXPLODE" },
    { 0x2000, "DAM_DESTROY" },
    { 0x4000, "DAM_DROP" },
    { 0x8000, "DAM_LOSE_TURN" },
    { 0x10000, "DAM_HIT_SELF" },
    { 0x20000, "DAM_LOSE_AMMO" },
    { 0x40000, "DAM_DUD" },
    { 0x80000, "DAM_HURT_SELF" },
    { 0x100000, "DAM_RANDOM_HIT" },
    { 0x200000, "DAM_CRIP_RANDOM" },
    { 0x400000, "DAM_BACKWASH" },
    { 0x800000, "DAM_PERFORM_REVERSE" },
} };

/// The set bits of `value` as engine names, with any bit the engine does not name reported as hex
/// rather than dropped.
template <std::size_t N>
nlohmann::ordered_json flagNames(uint32_t value, const std::array<std::pair<uint32_t, const char*>, N>& table) {
    auto names = nlohmann::ordered_json::array();
    uint32_t known = 0;
    for (const auto& [bit, name] : table) {
        if ((value & bit) != 0) {
            names.push_back(name);
        }
        known |= bit;
    }
    if ((value & ~known) != 0) {
        names.push_back(std::format("0x{:X}", value & ~known));
    }
    return names;
}

} // namespace geck::cli
