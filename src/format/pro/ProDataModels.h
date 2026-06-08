#pragma once

#include <cstdint>

#include "Pro.h"

namespace geck {

/**
 * @brief Data models for PRO file type-specific data
 *
 * These structures represent the various data sections that appear after the
 * common header in PRO files, depending on the object type.
 *
 * Where the editor's representation matches Pro's on-disk layout exactly, the
 * type is an alias of the canonical Pro definition so there is a single source
 * of truth (the two can no longer drift). Types whose editor representation
 * deliberately differs from the on-disk Pro layout keep their own struct here.
 */

// Identical to the canonical Pro definitions -> aliases.
using ProArmorData = Pro::ArmorData;
using ProContainerData = Pro::ContainerData;
using ProDrugData = Pro::DrugData;
using ProAmmoData = Pro::AmmoData;
using ProMiscData = Pro::MiscData;
using ProKeyData = Pro::KeyData;
using ProWallData = Pro::WallData;
using ProTileData = Pro::TileData;

// Item Type: Weapon
// Pro::WeaponData additionally carries weaponFlags, which the weapon widget
// manages directly on pro->weaponData; the editor struct intentionally omits it.
struct ProWeaponData {
    uint32_t animationCode;
    uint32_t damageMin, damageMax;
    uint32_t damageType;
    uint32_t rangePrimary, rangeSecondary;
    int32_t projectilePID;
    uint32_t minimumStrength;
    uint32_t actionCostPrimary, actionCostSecondary;
    uint32_t criticalFail;
    uint32_t perk;
    uint32_t burstRounds;
    uint32_t ammoType;
    int32_t ammoPID;
    uint32_t ammoCapacity;
    uint8_t soundId;
};

// Object Type: Critter
// Kept separate pending a full layout reconciliation against Pro::CritterData.
struct ProCritterData {
    uint32_t headFID;
    uint32_t aiPacket;
    uint32_t teamNumber;
    uint32_t flags;
    // SPECIAL stats (7 stats: STR, PER, END, CHR, INT, AGL, LCK)
    uint32_t specialStats[7];
    uint32_t maxHitPoints;
    uint32_t actionPoints;
    uint32_t armorClass;
    uint32_t unused;
    uint32_t meleeDamage;
    uint32_t carryWeightMax;
    uint32_t sequence;
    uint32_t healingRate;
    uint32_t criticalChance;
    uint32_t betterCriticals;
    // Damage threshold (7 damage types)
    uint32_t damageThreshold[7];
    // Damage resist (9 damage types)
    uint32_t damageResist[9];
    uint32_t age;
    uint32_t gender;
    // Bonus SPECIAL stats (7 stats)
    uint32_t bonusSpecialStats[7];
    uint32_t bonusHealthPoints;
    uint32_t bonusActionPoints;
    uint32_t bonusArmorClass;
    uint32_t bonusUnused;
    uint32_t bonusMeleeDamage;
    uint32_t bonusCarryWeight;
    uint32_t bonusSequence;
    uint32_t bonusHealingRate;
    uint32_t bonusCriticalChance;
    uint32_t bonusBetterCriticals;
    // Bonus damage threshold (8 values)
    uint32_t bonusDamageThreshold[8];
    // Bonus damage resistance (8 values)
    uint32_t bonusDamageResistance[8];
    uint32_t bonusAge;
    uint32_t bonusGender;
    // Skills (18 different skills)
    uint32_t skills[18];
    uint32_t bodyType;
    uint32_t experienceForKill;
    uint32_t killType;
    uint32_t damageType; // Optional field
};

// Object Type: Scenery
// The editor uses a union of subtype-specific data, whereas Pro::SceneryData
// stores every subtype struct separately; keep separate until reconciled with
// ProReader/ProWriter.
struct ProSceneryDoorData {
    uint32_t walkThroughFlag;
    uint32_t unknownField;
};

struct ProSceneryStairsData {
    uint32_t destTile;
    uint32_t destElevation;
};

struct ProSceneryElevatorData {
    uint32_t elevatorType;
    uint32_t elevatorLevel;
};

struct ProSceneryLadderData {
    uint32_t destTileAndElevation;
};

struct ProSceneryGenericData {
    uint32_t unknownField;
};

struct ProSceneryData {
    uint32_t materialId;
    uint8_t soundId;

    // Scenery subtype-specific data
    union {
        ProSceneryDoorData doorData;
        ProSceneryStairsData stairsData;
        ProSceneryElevatorData elevatorData;
        ProSceneryLadderData ladderData;
        ProSceneryGenericData genericData;
    };
};

} // namespace geck
