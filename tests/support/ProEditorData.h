#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "format/pro/Pro.h"
#include "support/DataTree.h"
#include "util/FalloutEngineEnums.h"

// The game text the PRO editor widgets label their choices from, plus bare PROs for them to edit.
namespace geck::test {

/// proto.msg naming every material, scenery type, body type, damage type and caliber.
inline std::string protoMsg() {
    std::string contents;

    const std::array<const char*, fallout::enumCount<fallout::MaterialType>()> materials = {
        "Glass", "Metal", "Plastic", "Wood", "Dirt", "Stone", "Cement", "Leather"
    };
    for (size_t index = 0; index < materials.size(); ++index) {
        contents += msgLine(fallout::protoMessageId(static_cast<fallout::MaterialType>(index)), materials[index]);
    }

    const std::array<const char*, fallout::enumCount<SceneryType>()> sceneryTypes = {
        "Door", "Stairs", "Elevator", "Ladder Bottom", "Ladder Top", "Generic"
    };
    for (size_t index = 0; index < sceneryTypes.size(); ++index) {
        contents += msgLine(fallout::protoMessageId(static_cast<SceneryType>(index)), sceneryTypes[index]);
    }

    const std::array<const char*, fallout::enumCount<fallout::BodyType>()> bodyTypes = {
        "Biped", "Quadruped", "Robotic"
    };
    for (size_t index = 0; index < bodyTypes.size(); ++index) {
        contents += msgLine(fallout::protoMessageId(static_cast<fallout::BodyType>(index)), bodyTypes[index]);
    }

    const std::array<const char*, fallout::enumCount<fallout::DamageType>()> damageTypes = {
        "Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion"
    };
    for (size_t index = 0; index < damageTypes.size(); ++index) {
        contents += msgLine(fallout::protoMessageId(static_cast<fallout::DamageType>(index)), damageTypes[index]);
    }

    const std::array<const char*, fallout::enumCount<fallout::CaliberType>()> calibers = {
        "None", "Rocket", "Flamethrower Fuel", "C Energy Cell", "D Energy Cell", ".223", "5mm", ".40 cal",
        "10mm", ".44 cal", "14mm", "12-gauge", "9mm", "BB", ".45 cal", "2mm", "4.7mm caseless",
        "HN needler", "7.62mm"
    };
    for (size_t index = 0; index < calibers.size(); ++index) {
        contents += msgLine(fallout::protoMessageId(static_cast<fallout::CaliberType>(index)), calibers[index]);
    }

    return contents;
}

/// perk.msg naming `namedPerks`, and every other perk "Perk <id>" when `includeAllPerks`.
inline std::string perkMsg(std::initializer_list<std::pair<fallout::PerkId, const char*>> namedPerks = {},
    bool includeAllPerks = false) {
    std::string contents;
    if (includeAllPerks) {
        for (int index = 0; index < fallout::enumValue(fallout::PerkId::Count); ++index) {
            contents += msgLine(fallout::perkNameMessageId(static_cast<fallout::PerkId>(index)), "Perk " + std::to_string(index));
        }
    }
    for (const auto& [perkId, label] : namedPerks) {
        contents += msgLine(fallout::perkNameMessageId(perkId), label);
    }
    return contents;
}

/// stat.msg naming every stat "Stat <id>", except a handful the tests look for by their real name.
inline std::string statMsg() {
    std::string contents;
    for (int index = 0; index < fallout::enumValue(fallout::StatId::Count); ++index) {
        const auto statId = static_cast<fallout::StatId>(index);
        std::string label = "Stat " + std::to_string(index);
        switch (statId) {
            case fallout::StatId::Strength:
                label = "Strength";
                break;
            case fallout::StatId::Perception:
                label = "Perception";
                break;
            case fallout::StatId::RadiationResistance:
                label = "Radiation Resistance";
                break;
            case fallout::StatId::PoisonResistance:
                label = "Poison Resistance";
                break;
            default:
                break;
        }
        contents += msgLine(fallout::statNameMessageId(statId), label);
    }
    return contents;
}

/// A bare item PRO of `itemType`.
inline std::shared_ptr<Pro> makeItemPro(ItemType itemType) {
    auto pro = std::make_shared<Pro>(std::filesystem::path("test.pro"));
    pro->header.PID = 0;
    pro->setObjectSubtypeId(static_cast<unsigned>(itemType));
    return pro;
}

/// A bare scenery PRO of `sceneryType`.
inline std::shared_ptr<Pro> makeSceneryPro(SceneryType sceneryType) {
    auto pro = std::make_shared<Pro>(std::filesystem::path("test.pro"));
    pro->header.PID = static_cast<int32_t>(ProtoId(ObjectType::Scenery, 0).pid());
    pro->setObjectSubtypeId(static_cast<unsigned>(sceneryType));
    return pro;
}

/// A bare critter PRO.
inline std::shared_ptr<Pro> makeCritterPro() {
    auto pro = std::make_shared<Pro>(std::filesystem::path("test.pro"));
    pro->header.PID = static_cast<int32_t>(ProtoId(ObjectType::Critter, 0).pid());
    return pro;
}

} // namespace geck::test
