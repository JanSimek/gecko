#pragma once

#include "../util/FalloutEngineEnums.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace geck::game::enums {

struct EnumOption {
    int value;
    QString label;
};

/**
 * @brief Damage types (7 basic types for armor/items)
 */
QStringList damageTypes7();

/**
 * @brief Damage types (9 types for critter resistance)
 */
QStringList damageTypes9();

/**
 * @brief Full stat names loaded from stat.msg using engine stat IDs
 */
QStringList statNames();

/**
 * @brief Material types for objects
 */
QStringList materialTypes();

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
 * @brief Ammo caliber types loaded from proto.msg
 */
QStringList ammoCaliberTypes();

/**
 * @brief Full perk options loaded from perk.msg using engine perk IDs
 */
QVector<EnumOption> allPerkOptions();

/**
 * @brief Weapon perk options loaded from perk.msg with raw perk IDs
 */
QVector<EnumOption> weaponPerkOptions();

/**
 * @brief Armor perk options loaded from perk.msg with raw perk IDs
 */
QVector<EnumOption> armorPerkOptions();

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
QStringList critterBodyTypes();

/**
 * @brief Scenery subtypes
 */
QStringList sceneryTypes();

/**
 * @brief Text file extensions (for syntax highlighting)
 */
inline QStringList textFileExtensions() {
    return { "cfg", "txt", "gam", "msg", "lst", "int", "ssl", "ini" };
}

} // namespace geck::game::enums
