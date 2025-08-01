#include "ProWriter.h"

#include <spdlog/spdlog.h>
#include "../WriterExceptions.h"

namespace geck {

bool ProWriter::write(const Pro& pro) {
    try {
        spdlog::debug("Writing PRO file: {}", getPath().string());
        
        auto& utils = getBinaryUtils();
        
        // Write header data
        writeHeader(pro);
        
        // Write type-specific data based on object type
        switch (pro.type()) {
            case Pro::OBJECT_TYPE::ITEM:
                writeItemData(pro);
                break;
            case Pro::OBJECT_TYPE::CRITTER:
                writeCritterData(pro);
                break;
            case Pro::OBJECT_TYPE::SCENERY:
                writeSceneryData(pro);
                break;
            case Pro::OBJECT_TYPE::WALL:
                writeWallData(pro);
                break;
            case Pro::OBJECT_TYPE::TILE:
                writeTileData(pro);
                break;
            case Pro::OBJECT_TYPE::MISC:
                writeMiscData(pro);
                break;
        }
        
        // Flush to ensure all data is written
        utils.flush();
        
        spdlog::debug("Successfully wrote PRO file: {} ({} bytes)", 
                      getPath().string(), utils.getBytesWritten());
        return true;
        
    } catch (const WriteException& e) {
        spdlog::error("Failed to write PRO file {}: {}", getPath().string(), e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Unexpected error writing PRO file {}: {}", getPath().string(), e.what());
        return false;
    }
}

void ProWriter::writeHeader(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write PRO header (all fields are big-endian)
    utils.writeBE32Signed(pro.header.PID);
    utils.writeBE32(pro.header.message_id);
    utils.writeBE32Signed(pro.header.FID);
    utils.writeBE32(pro.header.light_distance);
    utils.writeBE32(pro.header.light_intensity);
    utils.writeBE32(pro.header.flags);
    
    spdlog::trace("ProWriter: Wrote header for PID {}", pro.header.PID);
}

void ProWriter::writeItemData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write flagsExt field (not present for TILE and MISC types)
    switch (pro.type()) {
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            break;
        default:
            utils.writeBE32(pro.commonItemData.flagsExt);
            break;
    }
    
    // Write SID field (not present for TILE and MISC types)
    switch (pro.type()) {
        case Pro::OBJECT_TYPE::ITEM:
        case Pro::OBJECT_TYPE::CRITTER:
        case Pro::OBJECT_TYPE::SCENERY:
        case Pro::OBJECT_TYPE::WALL:
            utils.writeBE32(pro.commonItemData.SID);
            break;
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            break;
    }
    
    // Write object subtype ID
    utils.writeBE32(pro.objectSubtypeId());
    
    // Write common item data
    utils.writeBE32(pro.commonItemData.materialId);
    utils.writeBE32(pro.commonItemData.containerSize);
    utils.writeBE32(pro.commonItemData.weight);
    utils.writeBE32(pro.commonItemData.basePrice);
    utils.writeBE32Signed(pro.commonItemData.inventoryFID);
    utils.writeU8(pro.commonItemData.soundId);
    
    // Write item-type-specific data
    Pro::ITEM_TYPE itemType = static_cast<Pro::ITEM_TYPE>(pro.objectSubtypeId());
    
    switch (itemType) {
        case Pro::ITEM_TYPE::ARMOR:
            writeArmorData(pro);
            break;
        case Pro::ITEM_TYPE::CONTAINER:
            writeContainerData(pro);
            break;
        case Pro::ITEM_TYPE::DRUG:
            writeDrugData(pro);
            break;
        case Pro::ITEM_TYPE::WEAPON:
            writeWeaponData(pro);
            break;
        case Pro::ITEM_TYPE::AMMO:
            writeAmmoData(pro);
            break;
        case Pro::ITEM_TYPE::MISC:
            writeMiscItemData(pro);
            break;
        case Pro::ITEM_TYPE::KEY:
            writeKeyData(pro);
            break;
    }
    
    spdlog::trace("ProWriter: Wrote item data for type {}", static_cast<int>(itemType));
}

void ProWriter::writeArmorData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write armor class
    utils.writeBE32(pro.armorData.armorClass);
    
    // Write damage resistance array (7 damage types)
    for (int i = 0; i < Pro::DAMAGE_TYPES_ARMOR; ++i) {
        utils.writeBE32(pro.armorData.damageResist[i]);
    }
    
    // Write damage threshold array (7 damage types)
    for (int i = 0; i < Pro::DAMAGE_TYPES_ARMOR; ++i) {
        utils.writeBE32(pro.armorData.damageThreshold[i]);
    }
    
    // Write perk and FIDs
    utils.writeBE32(pro.armorData.perk);
    utils.writeBE32Signed(pro.armorData.armorMaleFID);
    utils.writeBE32Signed(pro.armorData.armorFemaleFID);
    
    spdlog::trace("ProWriter: Wrote armor data (AC: {})", pro.armorData.armorClass);
}

void ProWriter::writeContainerData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    utils.writeBE32(pro.containerData.maxSize);
    utils.writeBE32(pro.containerData.flags);
    
    spdlog::trace("ProWriter: Wrote container data (max size: {})", pro.containerData.maxSize);
}

void ProWriter::writeDrugData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write immediate effect stats
    utils.writeBE32(pro.drugData.stat0Base);
    utils.writeBE32(pro.drugData.stat1Base);
    utils.writeBE32(pro.drugData.stat2Base);
    utils.writeBE32Signed(pro.drugData.stat0Amount);
    utils.writeBE32Signed(pro.drugData.stat1Amount);
    utils.writeBE32Signed(pro.drugData.stat2Amount);
    
    // Write first delayed effect
    utils.writeBE32(pro.drugData.firstDelayMinutes);
    utils.writeBE32Signed(pro.drugData.firstStat0Amount);
    utils.writeBE32Signed(pro.drugData.firstStat1Amount);
    utils.writeBE32Signed(pro.drugData.firstStat2Amount);
    
    // Write second delayed effect
    utils.writeBE32(pro.drugData.secondDelayMinutes);
    utils.writeBE32Signed(pro.drugData.secondStat0Amount);
    utils.writeBE32Signed(pro.drugData.secondStat1Amount);
    utils.writeBE32Signed(pro.drugData.secondStat2Amount);
    
    // Write addiction data
    utils.writeBE32(pro.drugData.addictionChance);
    utils.writeBE32(pro.drugData.addictionPerk);
    utils.writeBE32(pro.drugData.addictionDelay);
    
    spdlog::trace("ProWriter: Wrote drug data (addiction chance: {}%)", pro.drugData.addictionChance);
}

void ProWriter::writeWeaponData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write weapon properties
    utils.writeBE32(pro.weaponData.animationCode);
    utils.writeBE32(pro.weaponData.damageMin);
    utils.writeBE32(pro.weaponData.damageMax);
    utils.writeBE32(pro.weaponData.damageType);
    utils.writeBE32(pro.weaponData.rangePrimary);
    utils.writeBE32(pro.weaponData.rangeSecondary);
    utils.writeBE32Signed(pro.weaponData.projectilePID);
    utils.writeBE32(pro.weaponData.minimumStrength);
    utils.writeBE32(pro.weaponData.actionCostPrimary);
    utils.writeBE32(pro.weaponData.actionCostSecondary);
    utils.writeBE32(pro.weaponData.criticalFail);
    utils.writeBE32(pro.weaponData.perk);
    utils.writeBE32(pro.weaponData.burstRounds);
    utils.writeBE32(pro.weaponData.ammoType);
    utils.writeBE32Signed(pro.weaponData.ammoPID);
    utils.writeBE32(pro.weaponData.ammoCapacity);
    utils.writeU8(pro.weaponData.soundId);
    
    // Write extended weapon flags (always write for new format compatibility)
    utils.writeBE32(pro.weaponData.weaponFlags);
    
    spdlog::trace("ProWriter: Wrote weapon data (damage: {}-{}, flags: 0x{:X})", 
                  pro.weaponData.damageMin, pro.weaponData.damageMax, pro.weaponData.weaponFlags);
}

void ProWriter::writeAmmoData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write ammo-specific data
    utils.writeBE32(pro.ammoData.caliber);
    utils.writeBE32(pro.ammoData.quantity);
    utils.writeBE32Signed(pro.ammoData.damageModifier);
    utils.writeBE32Signed(pro.ammoData.damageResistModifier);
    utils.writeBE32Signed(pro.ammoData.damageMultiplier);
    utils.writeBE32Signed(pro.ammoData.damageTypeModifier);
    
    spdlog::trace("ProWriter: Wrote ammo data (caliber: {}, quantity: {})", 
                  pro.ammoData.caliber, pro.ammoData.quantity);
}

void ProWriter::writeMiscItemData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write misc item data
    utils.writeBE32(pro.miscData.powerType);
    utils.writeBE32(pro.miscData.charges);
    
    spdlog::trace("ProWriter: Wrote misc item data (power type: {}, charges: {})", 
                  pro.miscData.powerType, pro.miscData.charges);
}

void ProWriter::writeKeyData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write key data
    utils.writeBE32(pro.keyData.keyId);
    
    spdlog::trace("ProWriter: Wrote key data (key ID: {})", pro.keyData.keyId);
}

void ProWriter::writeCritterData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    const auto& critterData = pro.critterData;
    
    // Write critter data from the structure
    utils.writeBE32(critterData.headFID);
    utils.writeBE32(critterData.aiPacket);
    utils.writeBE32(critterData.teamNumber);
    utils.writeBE32(critterData.flags);

    // S P E C I A L stats (7 base stats)
    for (int i = 0; i < Pro::SPECIAL_STATS_COUNT; ++i) {
        utils.writeBE32(critterData.specialStats[i]);
    }
    
    utils.writeBE32(critterData.maxHitPoints);
    utils.writeBE32(critterData.actionPoints);
    utils.writeBE32(critterData.armorClass);
    utils.writeBE32(critterData.unused);
    utils.writeBE32(critterData.meleeDamage);
    utils.writeBE32(critterData.carryWeightMax);
    utils.writeBE32(critterData.sequence);
    utils.writeBE32(critterData.healingRate);
    utils.writeBE32(critterData.criticalChance);
    utils.writeBE32(critterData.betterCriticals);

    // Damage threshold (7 damage types)
    for (int i = 0; i < Pro::DAMAGE_TYPES_ARMOR; ++i) {
        utils.writeBE32(critterData.damageThreshold[i]);
    }
    
    // Damage resist (9 damage types)
    for (int i = 0; i < Pro::DAMAGE_TYPES_CRITTER; ++i) {
        utils.writeBE32(critterData.damageResist[i]);
    }

    utils.writeBE32(critterData.age);
    utils.writeBE32(critterData.gender);

    // Bonus SPECIAL stats (7 stats)
    for (int i = 0; i < Pro::SPECIAL_STATS_COUNT; ++i) {
        utils.writeBE32(critterData.bonusSpecialStats[i]);
    }

    utils.writeBE32(critterData.bonusHealthPoints);
    utils.writeBE32(critterData.bonusActionPoints);
    utils.writeBE32(critterData.bonusArmorClass);
    utils.writeBE32(critterData.bonusUnused);
    utils.writeBE32(critterData.bonusMeleeDamage);
    utils.writeBE32(critterData.bonusCarryWeight);
    utils.writeBE32(critterData.bonusSequence);
    utils.writeBE32(critterData.bonusHealingRate);
    utils.writeBE32(critterData.bonusCriticalChance);
    utils.writeBE32(critterData.bonusBetterCriticals);

    // Bonus Damage threshold (8 values)
    for (int i = 0; i < Pro::BONUS_DAMAGE_ARRAYS; ++i) {
        utils.writeBE32(critterData.bonusDamageThreshold[i]);
    }

    // Bonus Damage resistance (8 values)
    for (int i = 0; i < Pro::BONUS_DAMAGE_ARRAYS; ++i) {
        utils.writeBE32(critterData.bonusDamageResistance[i]);
    }

    utils.writeBE32(critterData.bonusAge);
    utils.writeBE32(critterData.bonusGender);

    // Skills (18 different skills)
    for (int i = 0; i < Pro::SKILLS_COUNT; ++i) {
        utils.writeBE32(critterData.skills[i]);
    }

    utils.writeBE32(critterData.bodyType);
    utils.writeBE32(critterData.experienceForKill);
    utils.writeBE32(critterData.killType);
    utils.writeBE32(critterData.damageType);
    
    spdlog::debug("ProWriter: Critter data written - headFID: {}, aiPacket: {}, teamNumber: {}", 
                 critterData.headFID, critterData.aiPacket, critterData.teamNumber);
}

void ProWriter::writeSceneryData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    const auto& sceneryData = pro.sceneryData;
    
    // Write scenery subtype first
    utils.writeBE32(pro.objectSubtypeId());
    
    // Write scenery data from the structure
    utils.writeBE32(sceneryData.materialId);
    utils.writeU8(sceneryData.soundId);
    
    // Write subtype-specific data based on scenery type
    Pro::SCENERY_TYPE sceneryType = static_cast<Pro::SCENERY_TYPE>(pro.objectSubtypeId());
    
    switch (sceneryType) {
        case Pro::SCENERY_TYPE::DOOR:
            utils.writeBE32(sceneryData.doorData.walkThroughFlag);
            utils.writeBE32(sceneryData.doorData.unknownField);
            break;
        case Pro::SCENERY_TYPE::STAIRS:
            utils.writeBE32(sceneryData.stairsData.destTile);
            utils.writeBE32(sceneryData.stairsData.destElevation);
            break;
        case Pro::SCENERY_TYPE::ELEVATOR:
            utils.writeBE32(sceneryData.elevatorData.elevatorType);
            utils.writeBE32(sceneryData.elevatorData.elevatorLevel);
            break;
        case Pro::SCENERY_TYPE::LADDER_BOTTOM:
        case Pro::SCENERY_TYPE::LADDER_TOP:
            utils.writeBE32(sceneryData.ladderData.destTileAndElevation);
            break;
        case Pro::SCENERY_TYPE::GENERIC:
            utils.writeBE32(sceneryData.genericData.unknownField);
            break;
    }
    
    spdlog::debug("ProWriter: Scenery data written - materialId: {}, soundId: {}, type: {}", 
                 sceneryData.materialId, sceneryData.soundId, static_cast<int>(sceneryType));
}

void ProWriter::writeWallData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write wall data
    /*
     * TODO:
     *   0x0018	2	Wall Light Type Flags
     *   0x001A	2	Action Flags
     *   0x001C	4	ScriptType & ScriptID
     *   0x0020	4	MaterialID
     */
    utils.writeBE32(0); // flagsExt placeholder
    utils.writeBE32(0); // SID placeholder
    utils.writeBE32(pro.wallData.materialId);
    
    spdlog::debug("ProWriter: Wall data written - materialId: {}", pro.wallData.materialId);
}

void ProWriter::writeTileData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write tile data
    utils.writeBE32(pro.tileData.materialId);
    
    spdlog::debug("ProWriter: Tile data written - materialId: {}", pro.tileData.materialId);
}

void ProWriter::writeMiscData(const Pro& pro) {
    auto& utils = getBinaryUtils();
    
    // Write basic misc object data
    utils.writeBE32(0); // unknown field placeholder
    
    spdlog::debug("ProWriter: Basic misc data written (minimal implementation)");
}

} // namespace geck