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

    // Weapon flag constants
    enum class WEAPON_FLAGS : uint32_t {
        ENERGY_WEAPON = 0x00000001  // Forces weapon to use Energy Weapons skill (sfall 4.2/3.8.20)
    };

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
        uint32_t damageResist[7];     // Normal, Laser, Fire, Plasma, Electrical, EMP, Explosion
        uint32_t damageThreshold[7];
        uint32_t perk;
        int32_t armorMaleFID;
        int32_t armorFemaleFID;
    } armorData;

    struct ContainerData {
        uint32_t maxSize;
        uint32_t flags;  // Use, UseOn, Look, Talk, Pickup flags
    } containerData;

    struct DrugData {
        uint32_t stat0Base, stat1Base, stat2Base;
        int32_t stat0Amount, stat1Amount, stat2Amount;
        uint32_t firstDelayMinutes;
        int32_t firstStat0Amount, firstStat1Amount, firstStat2Amount;
        uint32_t secondDelayMinutes;
        int32_t secondStat0Amount, secondStat1Amount, secondStat2Amount;
        uint32_t addictionChance;
        uint32_t addictionPerk;
        uint32_t addictionDelay;
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
