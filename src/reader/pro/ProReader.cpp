#include "ProReader.h"

#include <spdlog/spdlog.h>

#include "../../format/pro/Pro.h"
#include "../ErrorMessages.h"

namespace geck {

/**
 * NOTE: All fields in PRO files are stored in big-endian format
 */
std::unique_ptr<Pro> ProReader::read() {
    try {
        // Use format validation
        FormatValidator::validateProFile(getBinaryUtils(), _path);
        
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading PRO file: {}", _path.string());

        auto pro = std::make_unique<Pro>(_path);

        pro->header.PID = utils.readBE32Signed();
        pro->header.message_id = utils.readBE32();
        pro->header.FID = utils.readBE32Signed();
        pro->header.light_distance = utils.readBE32();
        pro->header.light_intensity = utils.readBE32();
        pro->header.flags = utils.readBE32();

    switch (pro->type()) {
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            break;
        default:
            pro->commonItemData.flagsExt = utils.readBE32();
            break;
    }

    switch (pro->type()) {
        case Pro::OBJECT_TYPE::ITEM:
        case Pro::OBJECT_TYPE::CRITTER:
        case Pro::OBJECT_TYPE::SCENERY:
        case Pro::OBJECT_TYPE::WALL:
            pro->commonItemData.SID = utils.readBE32();
            break;
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            break;
    }

    switch (pro->type()) {
        case Pro::OBJECT_TYPE::ITEM: {
            uint32_t subtypeId = utils.readBE32();
            pro->setObjectSubtypeId(subtypeId);

            // Read common item data
            pro->commonItemData.materialId = utils.readBE32();
            pro->commonItemData.containerSize = utils.readBE32();
            pro->commonItemData.weight = utils.readBE32();
            pro->commonItemData.basePrice = utils.readBE32();
            pro->commonItemData.inventoryFID = utils.readBE32Signed();
            pro->commonItemData.soundId = utils.readU8();

            switch ((Pro::ITEM_TYPE)subtypeId) {
                case Pro::ITEM_TYPE::ARMOR: {
                    pro->armorData.armorClass = utils.readBE32();
                    for (int i = 0; i < Pro::DAMAGE_TYPES_ARMOR; ++i) {
                        pro->armorData.damageResist[i] = utils.readBE32();
                    }
                    for (int i = 0; i < Pro::DAMAGE_TYPES_ARMOR; ++i) {
                        pro->armorData.damageThreshold[i] = utils.readBE32();
                    }
                    pro->armorData.perk = utils.readBE32();
                    pro->armorData.armorMaleFID = utils.readBE32Signed();
                    pro->armorData.armorFemaleFID = utils.readBE32Signed();
                    break;
                }
                case Pro::ITEM_TYPE::CONTAINER: {
                    pro->containerData.maxSize = utils.readBE32();
                    pro->containerData.flags = utils.readBE32();
                    break;
                }
                case Pro::ITEM_TYPE::DRUG: {
                    pro->drugData.stat0Base = utils.readBE32();
                    pro->drugData.stat1Base = utils.readBE32();
                    pro->drugData.stat2Base = utils.readBE32();
                    pro->drugData.stat0Amount = utils.readBE32Signed();
                    pro->drugData.stat1Amount = utils.readBE32Signed();
                    pro->drugData.stat2Amount = utils.readBE32Signed();
                    // first delayed effect
                    pro->drugData.firstDelayMinutes = utils.readBE32();
                    pro->drugData.firstStat0Amount = utils.readBE32Signed();
                    pro->drugData.firstStat1Amount = utils.readBE32Signed();
                    pro->drugData.firstStat2Amount = utils.readBE32Signed();
                    // second delayed effect
                    pro->drugData.secondDelayMinutes = utils.readBE32();
                    pro->drugData.secondStat0Amount = utils.readBE32Signed();
                    pro->drugData.secondStat1Amount = utils.readBE32Signed();
                    pro->drugData.secondStat2Amount = utils.readBE32Signed();
                    pro->drugData.addictionChance = utils.readBE32();
                    pro->drugData.addictionPerk = utils.readBE32();
                    pro->drugData.addictionDelay = utils.readBE32();
                    break;
                }
                case Pro::ITEM_TYPE::WEAPON: {
                    pro->weaponData.animationCode = utils.readBE32();
                    pro->weaponData.damageMin = utils.readBE32();
                    pro->weaponData.damageMax = utils.readBE32();
                    pro->weaponData.damageType = utils.readBE32();
                    pro->weaponData.rangePrimary = utils.readBE32();
                    pro->weaponData.rangeSecondary = utils.readBE32();
                    pro->weaponData.projectilePID = utils.readBE32Signed();
                    pro->weaponData.minimumStrength = utils.readBE32();
                    pro->weaponData.actionCostPrimary = utils.readBE32();
                    pro->weaponData.actionCostSecondary = utils.readBE32();
                    pro->weaponData.criticalFail = utils.readBE32();
                    pro->weaponData.perk = utils.readBE32();
                    pro->weaponData.burstRounds = utils.readBE32();
                    pro->weaponData.ammoType = utils.readBE32();
                    pro->weaponData.ammoPID = utils.readBE32Signed();
                    pro->weaponData.ammoCapacity = utils.readBE32();
                    pro->weaponData.soundId = utils.readU8();
                    
                    // Extended weapon flags (optional field, may not exist in older PRO files)
                    if (utils.bytesRemaining() >= Pro::FIELD_SIZE_BYTES) {
                        pro->weaponData.weaponFlags = utils.readBE32();
                    } else {
                        pro->weaponData.weaponFlags = 0; // Default value for compatibility
                        spdlog::debug("Weapon PRO missing weapon flags field (older format)");
                    }
                    break;
                }
                case Pro::ITEM_TYPE::AMMO: {
                    pro->ammoData.caliber = utils.readBE32();
                    pro->ammoData.quantity = utils.readBE32();
                    pro->ammoData.damageModifier = utils.readBE32Signed();
                    pro->ammoData.damageResistModifier = utils.readBE32Signed();
                    pro->ammoData.damageMultiplier = utils.readBE32Signed();
                    pro->ammoData.damageTypeModifier = utils.readBE32Signed();
                    break;
                }
                case Pro::ITEM_TYPE::MISC: {
                    pro->miscData.powerType = utils.readBE32();
                    pro->miscData.charges = utils.readBE32();
                    break;
                }
                case Pro::ITEM_TYPE::KEY: {
                    pro->keyData.keyId = utils.readBE32();
                    break;
                }
            }
            break;
        }
        case Pro::OBJECT_TYPE::CRITTER: {
            // Read critter data into the structure
            auto& critterData = pro->critterData;
            
            critterData.headFID = utils.readBE32();
            critterData.aiPacket = utils.readBE32();
            critterData.teamNumber = utils.readBE32();
            critterData.flags = utils.readBE32();

            // S P E C I A L stats (7 base stats)
            for (int i = 0; i < Pro::SPECIAL_STATS_COUNT; ++i) {
                critterData.specialStats[i] = utils.readBE32();
            }
            
            critterData.maxHitPoints = utils.readBE32();
            critterData.actionPoints = utils.readBE32();
            critterData.armorClass = utils.readBE32();
            critterData.unused = utils.readBE32();
            critterData.meleeDamage = utils.readBE32();
            critterData.carryWeightMax = utils.readBE32();
            critterData.sequence = utils.readBE32();
            critterData.healingRate = utils.readBE32();
            critterData.criticalChance = utils.readBE32();
            critterData.betterCriticals = utils.readBE32();

            // Damage threshold (7 damage types)
            for (int i = 0; i < Pro::DAMAGE_TYPES_ARMOR; ++i) {
                critterData.damageThreshold[i] = utils.readBE32();
            }
            
            // Damage resist (9 damage types)
            for (int i = 0; i < Pro::DAMAGE_TYPES_CRITTER; ++i) {
                critterData.damageResist[i] = utils.readBE32();
            }

            critterData.age = utils.readBE32();
            critterData.gender = utils.readBE32();

            // Bonus SPECIAL stats (7 stats)
            for (int i = 0; i < Pro::SPECIAL_STATS_COUNT; ++i) {
                critterData.bonusSpecialStats[i] = utils.readBE32();
            }

            critterData.bonusHealthPoints = utils.readBE32();
            critterData.bonusActionPoints = utils.readBE32();
            critterData.bonusArmorClass = utils.readBE32();
            critterData.bonusUnused = utils.readBE32();
            critterData.bonusMeleeDamage = utils.readBE32();
            critterData.bonusCarryWeight = utils.readBE32();
            critterData.bonusSequence = utils.readBE32();
            critterData.bonusHealingRate = utils.readBE32();
            critterData.bonusCriticalChance = utils.readBE32();
            critterData.bonusBetterCriticals = utils.readBE32();

            // Bonus Damage threshold (8 values)
            for (int i = 0; i < Pro::BONUS_DAMAGE_ARRAYS; ++i) {
                critterData.bonusDamageThreshold[i] = utils.readBE32();
            }

            // Bonus Damage resistance (8 values)
            for (int i = 0; i < Pro::BONUS_DAMAGE_ARRAYS; ++i) {
                critterData.bonusDamageResistance[i] = utils.readBE32();
            }

            critterData.bonusAge = utils.readBE32();
            critterData.bonusGender = utils.readBE32();

            // Skills (18 different skills)
            for (int i = 0; i < Pro::SKILLS_COUNT; ++i) {
                critterData.skills[i] = utils.readBE32();
            }

            critterData.bodyType = utils.readBE32();
            critterData.experienceForKill = utils.readBE32();
            critterData.killType = utils.readBE32();
            
            // Damage type field is optional - certain maps (depolva, depolvb, kladwtwn) contains PRO files without it
            // Could be a remnant from Fallout 1 where the PRO format was 412 bytes vs 416 bytes in Fallout 2
            if (utils.bytesRemaining() >= Pro::FIELD_SIZE_BYTES) {
                critterData.damageType = utils.readBE32();
            } else {
                critterData.damageType = 0; // Default value for 412-byte format
                spdlog::debug("Critter PRO missing damage type field (412-byte format, likely Fallout 1 compatibility)");
            }
            
            spdlog::debug("ProReader: Loaded critter data - headFID: {}, aiPacket: {}, teamNumber: {}", 
                         critterData.headFID, critterData.aiPacket, critterData.teamNumber);
            break;
        }
        case Pro::OBJECT_TYPE::SCENERY: {
            uint32_t subtypeId = utils.readBE32();
            pro->setObjectSubtypeId(subtypeId);
            
            // Read scenery data into the structure
            auto& sceneryData = pro->sceneryData;

            sceneryData.materialId = utils.readBE32();
            sceneryData.soundId = utils.readU8();

            switch ((Pro::SCENERY_TYPE)subtypeId) {
                case Pro::SCENERY_TYPE::DOOR: {
                    sceneryData.doorData.walkThroughFlag = utils.readBE32();
                    sceneryData.doorData.unknownField = utils.readBE32();
                    break;
                }
                case Pro::SCENERY_TYPE::STAIRS: {
                    sceneryData.stairsData.destTile = utils.readBE32();
                    sceneryData.stairsData.destElevation = utils.readBE32();
                    break;
                }
                case Pro::SCENERY_TYPE::ELEVATOR: {
                    sceneryData.elevatorData.elevatorType = utils.readBE32();
                    sceneryData.elevatorData.elevatorLevel = utils.readBE32();
                    break;
                }
                case Pro::SCENERY_TYPE::LADDER_BOTTOM:
                case Pro::SCENERY_TYPE::LADDER_TOP: {
                    sceneryData.ladderData.destTileAndElevation = utils.readBE32();
                    break;
                }
                case Pro::SCENERY_TYPE::GENERIC: {
                    sceneryData.genericData.unknownField = utils.readBE32();
                    break;
                }
            }
            
            spdlog::debug("ProReader: Loaded scenery data - materialId: {}, soundId: {}, type: {}", 
                         sceneryData.materialId, sceneryData.soundId, static_cast<int>(subtypeId));
            break;
        }
        case Pro::OBJECT_TYPE::WALL: {
            pro->wallData.materialId = utils.readBE32();
            spdlog::debug("ProReader: Loaded wall data - materialId: {}", pro->wallData.materialId);
            break;
        }
        case Pro::OBJECT_TYPE::TILE: {
            pro->tileData.materialId = utils.readBE32();
            spdlog::debug("ProReader: Loaded tile data - materialId: {}", pro->tileData.materialId);
            break;
        }
        case Pro::OBJECT_TYPE::MISC: {
            utils.skipWithLog(Pro::FIELD_SIZE_BYTES, "misc unknown field");
            break;
        }
    }
    
    spdlog::debug("Successfully read PRO file: {} (type: {})", _path.string(), pro->typeToString());
    return pro;

    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException("Failed to parse PRO file: " + std::string(e.what()), _path);
    }
}

} // namespace geck
