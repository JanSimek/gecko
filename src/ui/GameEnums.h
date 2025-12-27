#pragma once

#include <QStringList>

namespace geck::game::enums {

/**
 * @brief Damage types (7 basic types for armor/items)
 */
inline QStringList damageTypes7() {
    return { "Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion" };
}

/**
 * @brief Damage types (9 types for critter resistance)
 * Includes Radiation and Poison
 */
inline QStringList damageTypes9() {
    return { "Normal", "Laser", "Fire", "Plasma", "Electrical",
        "EMP", "Explosion", "Radiation", "Poison" };
}

/**
 * @brief Material types for objects
 */
inline QStringList materialTypes() {
    return { "Glass", "Metal", "Plastic", "Wood",
        "Dirt", "Stone", "Cement", "Leather" };
}

/**
 * @brief Player/object orientations (6 hex directions)
 */
inline QStringList orientations() {
    return { "North-East", "East", "South-East",
        "South-West", "West", "North-West" };
}

/**
 * @brief Short orientation codes (6 hex directions)
 */
inline QStringList orientationsShort() {
    return { "NE", "E", "SE", "SW", "W", "NW" };
}

/**
 * @brief Map elevation levels
 */
inline QStringList elevations() {
    return { "Ground Level (0)", "1st Floor (1)", "2nd Floor (2)" };
}

/**
 * @brief Weapon animation types
 */
inline QStringList weaponAnimations() {
    return { "None", "Knife", "Club", "Sledgehammer", "Spear",
        "Pistol", "SMG", "Rifle", "Big Gun", "Minigun",
        "Rocket Launcher" };
}

/**
 * @brief Ammo caliber types
 */
inline QStringList ammoCaliberTypes() {
    return { "None", ".223", ".44 Magnum", "Flamethrower Fuel",
        "14mm", "2mm EC", "4.7mm Caseless", "9mm",
        "7.62mm", "Batteries", "Micro Fusion Cell",
        "Rocket", "Shotgun Shells", "Small Energy Cell", "HN Needler" };
}

// Constants for array sizing
constexpr int DAMAGE_TYPES_BASIC = 7;
constexpr int DAMAGE_TYPES_EXTENDED = 9;
constexpr int MATERIAL_TYPES = 8;
constexpr int ORIENTATIONS = 6;
constexpr int ELEVATIONS = 3;

} // namespace geck::game::enums
