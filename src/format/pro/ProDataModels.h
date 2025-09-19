#pragma once

#include <cstdint>

namespace geck {

/**
 * @brief Data models for PRO file type-specific data
 * 
 * These structures represent the various data sections that appear
 * after the common header in PRO files, depending on the object type.
 * 
 * Extracted from ProEditorDialog to follow Single Responsibility Principle
 * and enable reuse across different UI components.
 */

// Item Type: Armor
struct ProArmorData {
    uint32_t armorClass;
    uint32_t damageResist[7];     // Normal, Laser, Fire, Plasma, Electrical, EMP, Explosion
    uint32_t damageThreshold[7];
    uint32_t perk;
    int32_t armorMaleFID;
    int32_t armorFemaleFID;
};

// Item Type: Container
struct ProContainerData {
    uint32_t maxSize;
    uint32_t flags;  // Use, UseOn, Look, Talk, Pickup flags
};

// Item Type: Drug
struct ProDrugData {
    uint32_t stat0;                // Stat ID for immediate effect (0-14)
    uint32_t stat1;                // Stat ID for immediate effect (0-14)
    uint32_t stat2;                // Stat ID for immediate effect (0-14)
    int32_t amount0;               // Modifier for stat0 (signed)
    int32_t amount1;               // Modifier for stat1 (signed)
    int32_t amount2;               // Modifier for stat2 (signed)
    uint32_t duration1;            // Delay before first effect (game minutes)
    int32_t amount0_1;             // First delayed effect for stat0
    int32_t amount1_1;             // First delayed effect for stat1
    int32_t amount2_1;             // First delayed effect for stat2
    uint32_t duration2;            // Delay before second effect (game minutes)
    int32_t amount0_2;             // Second delayed effect for stat0
    int32_t amount1_2;             // Second delayed effect for stat1
    int32_t amount2_2;             // Second delayed effect for stat2
    uint32_t addictionRate;        // Addiction chance (percentage)
    uint32_t addictionEffect;      // Addiction perk ID
    uint32_t addictionOnset;       // Delay before addiction effect (game minutes)
};

// Item Type: Weapon
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

// Item Type: Ammo
struct ProAmmoData {
    uint32_t caliber;
    uint32_t quantity;
    int32_t damageModifier;
    int32_t damageResistModifier;
    int32_t damageMultiplier;
    int32_t damageTypeModifier;
};

// Item Type: Misc
struct ProMiscData {
    uint32_t powerType;
    uint32_t charges;
};

// Item Type: Key
struct ProKeyData {
    uint32_t keyId;
};

// Object Type: Critter
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
struct ProSceneryData {
    uint32_t materialId;
    uint8_t soundId;
    
    // Scenery subtype-specific data
    union {
        // Door-specific data
        struct {
            uint32_t walkThroughFlag;
            uint32_t unknownField;
        } doorData;
        
        // Stairs-specific data
        struct {
            uint32_t destTile;
            uint32_t destElevation;
        } stairsData;
        
        // Elevator-specific data
        struct {
            uint32_t elevatorType;
            uint32_t elevatorLevel;
        } elevatorData;
        
        // Ladder-specific data
        struct {
            uint32_t destTileAndElevation;
        } ladderData;
        
        // Generic-specific data
        struct {
            uint32_t unknownField;
        } genericData;
    };
};

// Object Type: Wall
struct ProWallData {
    uint32_t materialId;
};

// Object Type: Tile
struct ProTileData {
    uint32_t materialId;
};

} // namespace geck