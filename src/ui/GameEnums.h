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
 * @brief Ammo caliber types (19 types from proto.msg)
 * FIXME: read directly from proto.msg?
 */
inline QStringList ammoCaliberTypes() {
    return { "None", ".223 FMJ", "5mm JHP", "5mm AP", "10mm JHP", "10mm AP",
        ".44 Magnum JHP", ".44 Magnum FMJ", "14mm AP", "12 ga. Shotgun",
        "7.62mm", "9mm", "BB's", "Energy Cell", "Micro Fusion Cell",
        "Small Energy Cell", "Flamethrower Fuel", "Rocket", "Plasma" };
}

/**
 * @brief Weapon perks
 */
inline QStringList weaponPerks() {
    return { "None", "Fast Shot", "Long Range", "Accurate", "Penetrate",
        "Knockback", "Knockdown", "Flame", "Other" };
}

/**
 * @brief Armor perks
 */
inline QStringList armorPerks() {
    return { "None", "PowerArmor", "CombatArmor", "Other" };
}

/**
 * @brief Container action flags
 */
inline QStringList containerFlags() {
    return { "Use", "Use On", "Look", "Talk", "Pickup" };
}

/**
 * @brief Critter genders
 */
inline QStringList critterGenders() {
    return { "Male", "Female" };
}

/**
 * @brief Critter body types
 */
inline QStringList critterBodyTypes() {
    return { "Biped", "Quadruped", "Robotic" };
}

/**
 * @brief Scenery subtypes
 */
inline QStringList sceneryTypes() {
    return { "Door", "Stairs", "Elevator", "Ladder Bottom", "Ladder Top", "Generic" };
}

/**
 * @brief Text file extensions (for syntax highlighting)
 */
inline QStringList textFileExtensions() {
    return { "cfg", "txt", "gam", "msg", "lst", "int", "ssl", "ini" };
}

} // namespace geck::game::enums
