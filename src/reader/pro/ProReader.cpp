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
                    utils.skipWithLog(4, "container max size");
                    utils.skipWithLog(4, "container flags");
                    break;
                }
                case Pro::ITEM_TYPE::DRUG: {
                    utils.skipWithLog(4, "drug stat0 base");
                    utils.skipWithLog(4, "drug stat1 base");
                    utils.skipWithLog(4, "drug stat2 base");
                    utils.skipWithLog(4, "drug stat0 amount");
                    utils.skipWithLog(4, "drug stat1 amount");
                    utils.skipWithLog(4, "drug stat2 amount");
                    // first delayed effect
                    utils.skipWithLog(4, "drug first delay minutes");
                    utils.skipWithLog(4, "drug first stat0 amount");
                    utils.skipWithLog(4, "drug first stat1 amount");
                    utils.skipWithLog(4, "drug first stat2 amount");
                    // second delayed effect
                    utils.skipWithLog(4, "drug second delay minutes");
                    utils.skipWithLog(4, "drug second stat0 amount");
                    utils.skipWithLog(4, "drug second stat1 amount");
                    utils.skipWithLog(4, "drug second stat2 amount");
                    utils.skipWithLog(4, "drug addiction chance");
                    utils.skipWithLog(4, "drug addiction perk");
                    utils.skipWithLog(4, "drug addiction delay");
                    break;
                }
                case Pro::ITEM_TYPE::WEAPON:
                    utils.skipWithLog(4, "weapon animation code");
                    utils.skipWithLog(4, "weapon damage min");
                    utils.skipWithLog(4, "weapon damage max");
                    utils.skipWithLog(4, "weapon damage type");
                    utils.skipWithLog(4, "weapon range primary");
                    utils.skipWithLog(4, "weapon range secondary");
                    utils.skipWithLog(4, "weapon projectile PID");
                    utils.skipWithLog(4, "weapon minimum strength");
                    utils.skipWithLog(4, "weapon action cost primary");
                    utils.skipWithLog(4, "weapon action cost secondary");
                    utils.skipWithLog(4, "weapon critical fail");
                    utils.skipWithLog(4, "weapon perk");
                    utils.skipWithLog(4, "weapon burst rounds");
                    utils.skipWithLog(4, "weapon ammo type");
                    utils.skipWithLog(4, "weapon ammo PID");
                    utils.skipWithLog(4, "weapon ammo capacity");
                    utils.skipWithLog(1, "weapon sound ID");
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
            utils.skipWithLog(4, "critter head FID");

            utils.skipWithLog(4, "critter AI packet number");
            utils.skipWithLog(4, "critter team number");
            utils.skipWithLog(4, "critter flags");

            // S P E C I A L stats (7 base stats)
            utils.skipArray<uint32_t>(7, "critter SPECIAL stats (STR,PER,END,CHR,INT,AGL,LCK)");
            
            utils.skipWithLog(4, "critter max hit points");
            utils.skipWithLog(4, "critter action points");
            utils.skipWithLog(4, "critter armor class");
            utils.skipWithLog(4, "critter unused field");
            utils.skipWithLog(4, "critter melee damage");
            utils.skipWithLog(4, "critter carry weight max");
            utils.skipWithLog(4, "critter sequence");
            utils.skipWithLog(4, "critter healing rate");
            utils.skipWithLog(4, "critter critical chance");
            utils.skipWithLog(4, "critter better criticals");

            // Damage threshold (7 damage types)
            utils.skipArray<uint32_t>(7, "critter damage threshold array");
            
            // Damage resist (9 damage types)
            utils.skipArray<uint32_t>(9, "critter damage resist array");

            utils.skipWithLog(4, "critter age");
            utils.skipWithLog(4, "critter gender");

            // Bonus SPECIAL stats (7 stats)
            utils.skipArray<uint32_t>(7, "critter SPECIAL bonus stats");

            utils.skipWithLog(4, "critter bonus health points");
            utils.skipWithLog(4, "critter bonus action points");
            utils.skipWithLog(4, "critter bonus armor class");
            utils.skipWithLog(4, "critter bonus unused field");
            utils.skipWithLog(4, "critter bonus melee damage");
            utils.skipWithLog(4, "critter bonus carry weight");
            utils.skipWithLog(4, "critter bonus sequence");
            utils.skipWithLog(4, "critter bonus healing rate");
            utils.skipWithLog(4, "critter bonus critical chance");
            utils.skipWithLog(4, "critter bonus better criticals");

            // Bonus Damage threshold (8 values)
            utils.skipArray<uint32_t>(8, "critter bonus damage threshold array");

            // Bonus Damage resistance (8 values)
            utils.skipArray<uint32_t>(8, "critter bonus damage resistance array");

            utils.skipWithLog(4, "critter bonus age");
            utils.skipWithLog(4, "critter bonus gender");

            // Skills (18 different skills)
            utils.skipArray<uint32_t>(18, "critter skills array (18 skills)");

            utils.skipWithLog(4, "critter body type");
            utils.skipWithLog(4, "critter experience for kill");
            utils.skipWithLog(4, "critter kill type");
            
            // Damage type field is optional - certain maps (depolva, depolvb, kladwtwn) contains PRO files without it
            // Could be a remnant from Fallout 1 where the PRO format was 412 bytes vs 416 bytes in Fallout 2
            if (utils.bytesRemaining() >= 4) {
                utils.skipWithLog(4, "critter damage type");
            } else {
                spdlog::debug("Critter PRO missing damage type field (412-byte format, likely Fallout 1 compatibility)");
            }
            break;
        }
        case Pro::OBJECT_TYPE::SCENERY: {
            uint32_t subtypeId = utils.readBE32();
            pro->setObjectSubtypeId(subtypeId);

            utils.skipWithLog(4, "scenery material ID");
            utils.skipWithLog(1, "scenery sound ID");

            switch ((Pro::SCENERY_TYPE)subtypeId) {
                case Pro::SCENERY_TYPE::DOOR: {
                    utils.skipWithLog(4, "door walk through flag");
                    utils.skipWithLog(4, "door unknown field");
                    break;
                }
                case Pro::SCENERY_TYPE::STAIRS: {
                    utils.skipWithLog(4, "stairs dest tile");
                    utils.skipWithLog(4, "stairs dest elevation");
                    break;
                }
                case Pro::SCENERY_TYPE::ELEVATOR: {
                    utils.skipWithLog(4, "elevator type");
                    utils.skipWithLog(4, "elevator level");
                    break;
                }
                case Pro::SCENERY_TYPE::LADDER_BOTTOM:
                case Pro::SCENERY_TYPE::LADDER_TOP: {
                    utils.skipWithLog(4, "ladder dest tile and elevation");
                    break;
                }
                case Pro::SCENERY_TYPE::GENERIC: {
                    utils.skipWithLog(4, "generic scenery unknown field");
                    break;
                }
            }

            break;
        }
        case Pro::OBJECT_TYPE::WALL: {
            utils.skipWithLog(4, "wall material ID");
            break;
        }
        case Pro::OBJECT_TYPE::TILE: {
            utils.skipWithLog(4, "tile material ID");
            break;
        }
        case Pro::OBJECT_TYPE::MISC: {
            utils.skipWithLog(4, "misc unknown field");
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
