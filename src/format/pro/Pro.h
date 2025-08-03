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
    static constexpr int DAMAGE_TYPES_CRITTER = 9;       // Includes Radiation and Poison
    static constexpr int SKILLS_COUNT = 18;              // All Fallout 2 skills
    static constexpr int BONUS_DAMAGE_ARRAYS = 8;        // Extended arrays for critter bonuses
    static constexpr int FIELD_SIZE_BYTES = 4;           // Standard 32-bit field size
    
    // Weapon flag constants
    enum class WEAPON_FLAGS : uint32_t {
        ENERGY_WEAPON = 0x00000001  // Forces weapon to use Energy Weapons skill (sfall 4.2/3.8.20)
    };
    
    // Object Flags - from Fallout 2 CE obj_types.h
    enum class ObjectFlags : uint32_t {
        // Animation control flags (low 8 bits)
        ANIMATION_PRIMARY_MASK      = 0x0000000F,  // Primary attack animation index (bits 0-3)
        ANIMATION_SECONDARY_MASK    = 0x000000F0,  // Secondary attack animation index (bits 4-7)

        OBJECT_HIDDEN = 0x01,                // Object is hidden from view
        OBJECT_NO_SAVE = 0x04,              // Specifies that the object should not be saved to the savegame file
        OBJECT_FLAT = 0x08,                 // Flat object (no height)
        OBJECT_NO_BLOCK = 0x10,             // Does not block movement
        OBJECT_LIGHTING = 0x20,             // Has lighting
        OBJECT_NO_REMOVE = 0x400,           // Specifies that the object should not be removed (freed) from the game world
        OBJECT_MULTIHEX = 0x800,            // Occupies multiple hexes
        OBJECT_NO_HIGHLIGHT = 0x1000,       // Cannot be highlighted
        OBJECT_QUEUED = 0x2000,             // Set if there was/is any event for the object
        OBJECT_TRANS_RED = 0x4000,          // Red transparency
        OBJECT_TRANS_NONE = 0x8000,         // No transparency
        OBJECT_TRANS_WALL = 0x10000,        // Wall transparency
        OBJECT_TRANS_GLASS = 0x20000,       // Glass transparency
        OBJECT_TRANS_STEAM = 0x40000,       // Steam transparency
        OBJECT_TRANS_ENERGY = 0x80000,      // Energy transparency
        OBJECT_IN_LEFT_HAND = 0x1000000,    // In left hand
        OBJECT_IN_RIGHT_HAND = 0x2000000,   // In right hand
        OBJECT_WORN = 0x4000000,            // Being worn
        OBJECT_WALL_TRANS_END = 0x10000000, // Wall transparency end
        OBJECT_LIGHT_THRU = 0x20000000,     // Light passes through
        OBJECT_SEEN = 0x40000000,           // Has been seen
        OBJECT_SHOOT_THRU = 0x80000000,     // Can shoot through
        
        // Composite flags
        OBJECT_IN_ANY_HAND = OBJECT_IN_LEFT_HAND | OBJECT_IN_RIGHT_HAND,
        OBJECT_EQUIPPED = OBJECT_IN_ANY_HAND | OBJECT_WORN,
        OBJECT_OPEN_DOOR = OBJECT_SHOOT_THRU | OBJECT_LIGHT_THRU | OBJECT_NO_BLOCK,
    };
    
    // Critter Flags - from Fallout 2 CE obj_types.h
    enum class CritterFlags : uint32_t {
        CRITTER_BARTER = 0x02,         // Can barter with
        CRITTER_NO_STEAL = 0x20,       // Cannot steal from
        CRITTER_NO_DROP = 0x40,        // Cannot drop items
        CRITTER_NO_LIMBS = 0x80,       // No limb damage
        CRITTER_NO_AGE = 0x100,        // Does not age
        CRITTER_NO_HEAL = 0x200,       // Cannot heal
        CRITTER_INVULNERABLE = 0x400,  // Cannot be damaged
        CRITTER_FLAT = 0x800,          // Flat critter
        CRITTER_SPECIAL_DEATH = 0x1000, // Special death animation
        CRITTER_LONG_LIMBS = 0x2000,   // Has long limbs
        CRITTER_NO_KNOCKBACK = 0x4000, // Cannot be knocked back
    };
    
    // Extended Item Flags
    enum class ExtendedItemFlags : uint32_t {
        
        // Weapon behavior flags
        BIG_GUN                     = 0x00000100,  // Forces weapon to use Big Guns skill instead of Small Guns
        TWO_HANDED                  = 0x00000200,  // Weapon requires both hands, prevents dual-wielding
        
        // Action/interaction flags
        CAN_USE                     = 0x00000800,  // Item can be "used" (containers get this automatically)
        CAN_USE_ON                  = 0x00001000,  // Item can be "used on" target (drugs get this automatically)
        GENERAL_FLAG                = 0x00002000,  // General purpose flag (scenery/walls/tiles)
        INTERACTION_FLAG            = 0x00008000,  // Related to item interactions
        
        // Special item flags
        // This flag is used on weapons to indicate that's an natural (integral)
        // part of it's owner, for example Claw, or Robot's Rocket Launcher. Items
        // with this flag on do count toward total weight and cannot be dropped.
        ITEM_HIDDEN                 = 0x08000000,  // Item is integral part of owner, cannot be dropped (creature weapons)
    };

    // Extended flags helper functions
    static constexpr uint32_t getAnimationPrimary(uint32_t flags) {
        return flags & static_cast<uint32_t>(ObjectFlags::ANIMATION_PRIMARY_MASK);
    }
    
    static constexpr uint32_t getAnimationSecondary(uint32_t flags) {
        return (flags & static_cast<uint32_t>(ObjectFlags::ANIMATION_SECONDARY_MASK)) >> 4;
    }
    
    static constexpr uint32_t setAnimationPrimary(uint32_t flags, uint32_t animation) {
        return (flags & ~static_cast<uint32_t>(ObjectFlags::ANIMATION_PRIMARY_MASK)) | (animation & 0xF);
    }
    
    static constexpr uint32_t setAnimationSecondary(uint32_t flags, uint32_t animation) {
        return (flags & ~static_cast<uint32_t>(ObjectFlags::ANIMATION_SECONDARY_MASK)) | ((animation & 0xF) << 4);
    }
    
    // Helper function to check if a flag is set
    template<typename FlagEnum>
    static constexpr bool hasFlag(uint32_t flags, FlagEnum flag) {
        return (flags & static_cast<uint32_t>(flag)) != 0;
    }
    
    // Helper function to set a flag
    template<typename FlagEnum>
    static constexpr uint32_t setFlag(uint32_t flags, FlagEnum flag) {
        return flags | static_cast<uint32_t>(flag);
    }
    
    // Helper function to clear a flag
    template<typename FlagEnum>
    static constexpr uint32_t clearFlag(uint32_t flags, FlagEnum flag) {
        return flags & ~static_cast<uint32_t>(flag);
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
        // instant effects
        int32_t amount0;              // Modifier for stat0 (signed)
        int32_t amount1;              // Modifier for stat1 (signed)
        int32_t amount2;              // Modifier for stat2 (signed)
        // mid-delayed effects
        uint32_t duration1;           // Delay before first effect (game minutes)
        int32_t amount0_1;            // First delayed effect for stat0
        int32_t amount1_1;            // First delayed effect for stat1
        int32_t amount2_1;            // First delayed effect for stat2
        // long-delayed effects
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
