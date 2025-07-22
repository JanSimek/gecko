#include "ProReader.h"

#include <spdlog/spdlog.h>

#include "../../format/pro/Pro.h"
#include "../ErrorMessages.h"

namespace geck {

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
            utils.skipWithLog(4, "_flagsExt field");
            break;
    }

    switch (pro->type()) {
        case Pro::OBJECT_TYPE::ITEM:
        case Pro::OBJECT_TYPE::CRITTER:
        case Pro::OBJECT_TYPE::SCENERY:
        case Pro::OBJECT_TYPE::WALL:
            utils.skipWithLog(4, "_SID field");
            break;
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            break;
    }

    switch (pro->type()) {
        case Pro::OBJECT_TYPE::ITEM: {
            uint32_t subtypeId = utils.readBE32();
            pro->setObjectSubtypeId(subtypeId);

            utils.skipWithLog(4, "_materialId");
            utils.skipWithLog(4, "_containerSize");
            utils.skipWithLog(4, "_weight");
            utils.skipWithLog(4, "_basePrice");
            utils.skipWithLog(4, "_inventoryFID");
            utils.skipWithLog(1, "_soundId");

            switch ((Pro::ITEM_TYPE)subtypeId) {
                case Pro::ITEM_TYPE::ARMOR: {
                    utils.skipWithLog(4, "_armorClass");
                    utils.skipWithLog(7 * 4, "damage resist array (7 elements)");
                    utils.skipWithLog(7 * 4, "damage threshold array (7 elements)");
                    utils.skipWithLog(4, "_perk");
                    utils.skipWithLog(4, "_armorMaleFID");
                    utils.skipWithLog(4, "_armorFemaleFID");
                    break;
                }
                case Pro::ITEM_TYPE::CONTAINER: {
                    read_be_u32(); // stream.uint32(); // max size
                    read_be_u32(); // stream.uint32(); // containter flags
                    break;
                }
                case Pro::ITEM_TYPE::DRUG: {
                    read_be_u32(); // stream.uint32(); // Stat0
                    read_be_u32(); // stream.uint32(); // Stat1
                    read_be_u32(); // stream.uint32(); // Stat2
                    read_be_u32(); // stream.uint32(); // Stat0 ammount
                    read_be_u32(); // stream.uint32(); // Stat1 ammount
                    read_be_u32(); // stream.uint32(); // Stat2 ammount
                    // first delayed effect
                    read_be_u32(); // stream.uint32(); // delay in game minutes
                    read_be_u32(); // stream.uint32(); // Stat0 ammount
                    read_be_u32(); // stream.uint32(); // Stat1 ammount
                    read_be_u32(); // stream.uint32(); // Stat2 ammount
                    // second delayed effect
                    read_be_u32(); // stream.uint32(); // delay in game minutes
                    read_be_u32(); // stream.uint32(); // Stat0 ammount
                    read_be_u32(); // stream.uint32(); // Stat1 ammount
                    read_be_u32(); // stream.uint32(); // Stat2 ammount
                    read_be_u32(); // stream.uint32(); // addiction chance
                    read_be_u32(); // stream.uint32(); // addiction perk
                    read_be_u32(); // stream.uint32(); // addiction delay
                    break;
                }
                case Pro::ITEM_TYPE::WEAPON:
                    read_be_u32(); //_weaponAnimationCode  = stream.uint32();
                    read_be_u32(); //_weaponDamageMin      = stream.uint32();
                    read_be_u32(); //_weaponDamageMax      = stream.uint32();
                    read_be_u32(); //_weaponDamageType     = stream.uint32();
                    read_be_u32(); //_weaponRangePrimary   = stream.uint32();
                    read_be_u32(); //_weaponRangeSecondary = stream.uint32();
                    read_be_u32(); // stream.uint32(); // Proj PID
                    read_be_u32(); //_weaponMinimumStrenght     = stream.uint32();
                    read_be_u32(); //_weaponActionCostPrimary   = stream.uint32();
                    read_be_u32(); //_weaponActionCostSecondary = stream.uint32();
                    read_be_u32(); // stream.uint32(); // Crit Fail
                    read_be_u32(); //_perk = stream.int32();
                    read_be_u32(); //_weaponBurstRounds  = stream.uint32();
                    read_be_u32(); //_weaponAmmoType     = stream.uint32();
                    read_be_u32(); //_weaponAmmoPID      = stream.uint32();
                    read_be_u32(); //_weaponAmmoCapacity = stream.uint32();
                    read_be_u8();  //_soundId = stream.uint8();
                    break;
                case Pro::ITEM_TYPE::AMMO:
                    break;
                case Pro::ITEM_TYPE::MISC:
                    break;
                case Pro::ITEM_TYPE::KEY:
                    break;
            }
            break;
        }
        case Pro::OBJECT_TYPE::CRITTER: {
            read_be_i32(); //_critterHeadFID = stream.int32();

            read_be_u32(); // stream.uint32(); // ai packet number
            read_be_u32(); // stream.uint32(); // team number
            read_be_u32(); //_critterFlags = stream.uint32();

            // S P E C I A L
            for (unsigned int i = 0; i != 7; ++i) {
                read_be_u32(); //_critterStats.at(i) = stream.uint32();
            }
            read_be_u32(); //_critterHitPointsMax = stream.uint32();
            read_be_u32(); //_critterActionPoints = stream.uint32();
            read_be_u32(); //_critterArmorClass   = stream.uint32();
            read_be_u32(); // stream.uint32(); // Unused
            read_be_u32(); //_critterMeleeDamage    = stream.uint32();
            read_be_u32(); // _critterCarryWeightMax = stream.uint32();
            read_be_u32(); //_critterSequence       = stream.uint32();
            read_be_u32(); //_critterHealingRate    = stream.uint32();
            read_be_u32(); //_critterCriticalChance = stream.uint32();
            read_be_u32(); // stream.uint32(); // Better criticals

            // Damage threshold
            for (unsigned int i = 0; i != 7; ++i) {
                read_be_u32(); //_damageThreshold.at(i) = stream.uint32();
            }
            // Damage resist
            for (unsigned int i = 0; i != 9; ++i) {
                read_be_u32(); //_damageResist.at(i) = stream.uint32();
            }

            read_be_u32(); //_critterAge = stream.uint32(); // age
            read_be_u32(); //_critterGender = stream.uint32(); // sex

            for (unsigned int i = 0; i != 7; ++i) {
                read_be_u32(); //_critterStatsBonus.at(i) = stream.uint32();
            }

            read_be_u32(); // stream.uint32(); // Bonus Health points
            read_be_u32(); // stream.uint32(); // Bonus Action points
            read_be_u32(); // stream.uint32(); // Bonus Armor class
            read_be_u32(); // stream.uint32(); // Bonus Unused
            read_be_u32(); // stream.uint32(); // Bonus Melee damage
            read_be_u32(); // stream.uint32(); // Bonus Carry weight
            read_be_u32(); // stream.uint32(); // Bonus Sequence
            read_be_u32(); // stream.uint32(); // Bonus Healing rate
            read_be_u32(); // stream.uint32(); // Bonus Critical chance
            read_be_u32(); // stream.uint32(); // Bonus Better criticals

            // Bonus Damage threshold
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();

            // Bonus Damage resistance
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();
            read_be_u32();

            read_be_u32(); // Bonus age
            read_be_u32(); // Bonus sex

            for (unsigned int i = 0; i != 18; ++i) {
                read_be_u32(); //_critterSkills.at(i) = stream.uint32();
            }

            read_be_u32(); // body type
            read_be_u32(); // experience for kill
            read_be_u32(); // kill type
            read_be_u32(); // damage type
            break;
        }
        case Pro::OBJECT_TYPE::SCENERY: {
            uint32_t subtypeId = read_be_u32(); // _subtypeId  = stream.uint32();
            pro->setObjectSubtypeId(subtypeId);

            read_be_u32(); //_materialId = stream.uint32();
            read_be_u8();  // _soundId    = stream.uint8();

            switch ((Pro::SCENERY_TYPE)subtypeId) {
                case Pro::SCENERY_TYPE::DOOR: {
                    read_be_u32(); // stream.uint32(); // walk thru flag
                    read_be_u32(); // stream.uint32(); // unknown
                    break;
                }
                case Pro::SCENERY_TYPE::STAIRS: {
                    read_be_u32(); // stream.uint32(); // DestTile && DestElevation
                    read_be_u32(); // stream.uint32(); // DestElevation
                    break;
                }
                case Pro::SCENERY_TYPE::ELEVATOR: {
                    read_be_u32(); // stream.uint32(); // Elevator type
                    read_be_u32(); // stream.uint32(); // Elevator level
                    break;
                }
                case Pro::SCENERY_TYPE::LADDER_BOTTOM:
                case Pro::SCENERY_TYPE::LADDER_TOP: {
                    read_be_u32(); // stream.uint32(); // DestTile && DestElevation
                    break;
                }
                case Pro::SCENERY_TYPE::GENERIC: {
                    read_be_u32(); // stream.uint32(); // unknown
                    break;
                }
            }

            break;
        }
        case Pro::OBJECT_TYPE::WALL: {
            read_be_u32(); //_materialId = stream.uint32();
            break;
        }
        case Pro::OBJECT_TYPE::TILE: {
            read_be_u32(); //_materialId = stream.uint32();
            break;
        }
        case Pro::OBJECT_TYPE::MISC: {
            read_be_u32(); // stream.uint32(); // unknown
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
