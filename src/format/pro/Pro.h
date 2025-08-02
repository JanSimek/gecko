#pragma once

#include <istream>
#include <filesystem>

#include "../IFile.h"

namespace geck {

class Pro : public IFile {
private:
    unsigned int _objectSubtypeId;

public:
    Pro(std::filesystem::path path);
    
    void initializeDataStructures();

    enum class OBJECT_TYPE : uint32_t {
        ITEM = 0,
        CRITTER,
        SCENERY,
        WALL,
        TILE,
        MISC
    };

    enum class ITEM_TYPE : uint32_t {
        ARMOR = 0,
        CONTAINER,
        DRUG,
        WEAPON,
        AMMO,
        MISC,
        KEY
    };

    enum class SCENERY_TYPE : uint32_t {
        DOOR = 0,
        STAIRS,
        ELEVATOR,
        LADDER_BOTTOM,
        LADDER_TOP,
        GENERIC
    };

    // PRO file format constants
    static constexpr int SPECIAL_STATS_COUNT = 7;        // STR, PER, END, CHR, INT, AGL, LCK
    static constexpr int DAMAGE_TYPES_ARMOR = 7;         // Normal, Laser, Fire, Plasma, Electrical, EMP, Explosion
    static constexpr int DAMAGE_TYPES_CRITTER = 9;       // Includes Gas and Radiation
    static constexpr int SKILLS_COUNT = 18;              // All Fallout 2 skills
    static constexpr int BONUS_DAMAGE_ARRAYS = 8;        // Extended arrays for critter bonuses
    static constexpr int FIELD_SIZE_BYTES = 4;           // Standard 32-bit field size
    
    // Weapon flag constants
    enum class WEAPON_FLAGS : uint32_t {
        ENERGY_WEAPON = 0x00000001  // Forces weapon to use Energy Weapons skill (sfall 4.2/3.8.20)
    };
    
    // Extended Flags constants (flags_ext field) - based on Fallout 2 engine analysis
    enum class EXTENDED_FLAGS : uint32_t {
        // Animation control flags (low 8 bits)
        ANIMATION_PRIMARY_MASK      = 0x0000000F,  // Primary attack animation index (bits 0-3)
        ANIMATION_SECONDARY_MASK    = 0x000000F0,  // Secondary attack animation index (bits 4-7)
        
        // Weapon behavior flags
        BIG_GUN                     = 0x00000100,  // Forces weapon to use Big Guns skill instead of Small Guns
        TWO_HANDED                  = 0x00000200,  // Weapon requires both hands, prevents dual-wielding
        
        // Action/interaction flags
        CAN_USE                     = 0x00000800,  // Item can be "used" (containers get this automatically)
        CAN_USE_ON                  = 0x00001000,  // Item can be "used on" target (drugs get this automatically)
        GENERAL_FLAG                = 0x00002000,  // General purpose flag (scenery/walls/tiles)
        INTERACTION_FLAG            = 0x00008000,  // Related to item interactions
        
        // Special item flags
        ITEM_HIDDEN                 = 0x08000000,  // Item is integral part of owner, cannot be dropped (creature weapons)
        
        // Light/rendering flags (high bits)
        LIGHT_FLAG_1                = 0x10000000,  // Light rendering flag
        LIGHT_FLAG_2                = 0x20000000,  // Light rendering flag
        LIGHT_FLAG_3                = 0x40000000,  // Light rendering flag
        LIGHT_FLAG_4                = 0x80000000   // Light rendering flag
    };
    
    // Extended flags helper functions
    static constexpr uint32_t getAnimationPrimary(uint32_t flags) {
        return flags & static_cast<uint32_t>(EXTENDED_FLAGS::ANIMATION_PRIMARY_MASK);
    }
    
    static constexpr uint32_t getAnimationSecondary(uint32_t flags) {
        return (flags & static_cast<uint32_t>(EXTENDED_FLAGS::ANIMATION_SECONDARY_MASK)) >> 4;
    }
    
    static constexpr uint32_t setAnimationPrimary(uint32_t flags, uint32_t animation) {
        return (flags & ~static_cast<uint32_t>(EXTENDED_FLAGS::ANIMATION_PRIMARY_MASK)) | (animation & 0xF);
    }
    
    static constexpr uint32_t setAnimationSecondary(uint32_t flags, uint32_t animation) {
        return (flags & ~static_cast<uint32_t>(EXTENDED_FLAGS::ANIMATION_SECONDARY_MASK)) | ((animation & 0xF) << 4);
    }

    struct ProHeader {
        int32_t PID;
        uint32_t message_id;
        int32_t FID;
        uint32_t light_distance;
        uint32_t light_intensity;
        uint32_t flags;
    } header;

    // Extended data structures for different item types
    struct CommonItemData {
        uint32_t flagsExt;
        uint32_t SID;
        uint32_t materialId;
        uint32_t containerSize;
        uint32_t weight;
        uint32_t basePrice;
        int32_t inventoryFID;
        uint8_t soundId;
    } commonItemData;

    struct ArmorData {
        uint32_t armorClass;
        uint32_t damageResist[DAMAGE_TYPES_ARMOR];     // Normal, Laser, Fire, Plasma, Electrical, EMP, Explosion
        uint32_t damageThreshold[DAMAGE_TYPES_ARMOR];
        uint32_t perk;
        int32_t armorMaleFID;
        int32_t armorFemaleFID;
    } armorData;

    struct ContainerData {
        uint32_t maxSize;
        uint32_t flags;  // Use, UseOn, Look, Talk, Pickup flags
    } containerData;

    struct DrugData {
        uint32_t stat0;                // Stat ID for immediate effect (0-14)
        uint32_t stat1;                // Stat ID for immediate effect (0-14)
        uint32_t stat2;                // Stat ID for immediate effect (0-14)
        int32_t amount0;              // Modifier for stat0 (signed)
        int32_t amount1;              // Modifier for stat1 (signed)
        int32_t amount2;              // Modifier for stat2 (signed)
        uint32_t duration1;           // Delay before first effect (game minutes)
        int32_t amount0_1;            // First delayed effect for stat0
        int32_t amount1_1;            // First delayed effect for stat1
        int32_t amount2_1;            // First delayed effect for stat2
        uint32_t duration2;           // Delay before second effect (game minutes)
        int32_t amount0_2;            // Second delayed effect for stat0
        int32_t amount1_2;            // Second delayed effect for stat1
        int32_t amount2_2;            // Second delayed effect for stat2
        uint32_t addictionRate;       // Addiction chance (percentage)
        uint32_t addictionEffect;     // Addiction perk ID
        uint32_t addictionOnset;      // Delay before addiction effect (game minutes)
    } drugData;

    struct WeaponData {
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
        uint32_t weaponFlags;    // Extended weapon flags (energy weapon flag, etc.)
    } weaponData;

    struct AmmoData {
        uint32_t caliber;
        uint32_t quantity;
        int32_t damageModifier;
        int32_t damageResistModifier;
        int32_t damageMultiplier;
        int32_t damageTypeModifier;
    } ammoData;

    struct MiscData {
        uint32_t powerType;
        uint32_t charges;
    } miscData;

    struct KeyData {
        uint32_t keyId;
    } keyData;

    struct CritterData {
        uint32_t headFID;
        uint32_t aiPacket;
        uint32_t teamNumber;
        uint32_t flags;
        // SPECIAL stats (7 stats: STR, PER, END, CHR, INT, AGL, LCK)
        uint32_t specialStats[SPECIAL_STATS_COUNT];
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
        uint32_t damageThreshold[DAMAGE_TYPES_ARMOR];
        // Damage resist (9 damage types)
        uint32_t damageResist[DAMAGE_TYPES_CRITTER];
        uint32_t age;
        uint32_t gender;
        // Bonus SPECIAL stats (7 stats)
        uint32_t bonusSpecialStats[SPECIAL_STATS_COUNT];
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
        uint32_t bonusDamageThreshold[BONUS_DAMAGE_ARRAYS];
        // Bonus damage resistance (8 values)
        uint32_t bonusDamageResistance[BONUS_DAMAGE_ARRAYS];
        uint32_t bonusAge;
        uint32_t bonusGender;
        // Skills (18 different skills)
        uint32_t skills[SKILLS_COUNT];
        uint32_t bodyType;
        uint32_t experienceForKill;
        uint32_t killType;
        uint32_t damageType; // Optional field
    } critterData;

    struct SceneryData {
        uint32_t materialId;
        uint8_t soundId;
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
    } sceneryData;

    struct WallData {
        uint32_t materialId;
    } wallData;

    struct TileData {
        uint32_t materialId;
    } tileData;

    unsigned int objectSubtypeId() const;
    void setObjectSubtypeId(unsigned int objectSubtypeId);

    OBJECT_TYPE type() const;
    ITEM_TYPE itemType() const;

    const std::string typeToString() const;
    
    // Allow updating the file path for save operations
    void setPath(const std::filesystem::path& newPath) {
        _path = newPath;
        _filename = newPath.filename().string();
    }
};

} // namespace geck
