#include <catch2/catch_test_macros.hpp>

#include "util/FalloutEngineEnums.h"

TEST_CASE("Proto enums mirror Fallout 2 engine values", "[engine][enum]") {
    using namespace geck::fallout;

    REQUIRE(enumValue(ItemType::Armor) == 0);
    REQUIRE(enumValue(ItemType::Ammo) == 4);
    REQUIRE(protoMessageId(ItemType::Armor) == 150);
    REQUIRE(protoMessageId(ItemType::Ammo) == 154);

    REQUIRE(enumValue(MaterialType::Glass) == 0);
    REQUIRE(enumValue(MaterialType::Leather) == 7);
    REQUIRE(protoMessageId(MaterialType::Glass) == 100);
    REQUIRE(protoMessageId(MaterialType::Leather) == 107);

    REQUIRE(enumValue(DamageType::Normal) == 0);
    REQUIRE(enumValue(DamageType::Explosion) == 6);
    REQUIRE(protoMessageId(DamageType::Normal) == 250);
    REQUIRE(protoMessageId(DamageType::Explosion) == 256);

    REQUIRE(enumValue(CaliberType::None) == 0);
    REQUIRE(enumValue(CaliberType::Mm10) == 8);
    REQUIRE(enumValue(CaliberType::Mm7_62) == 18);
    REQUIRE(protoMessageId(CaliberType::Mm10) == 308);
    REQUIRE(protoMessageId(CaliberType::Mm7_62) == 318);
    REQUIRE(enumCount<CaliberType>() == 19);

    REQUIRE(enumValue(BodyType::Biped) == 0);
    REQUIRE(enumValue(BodyType::Robotic) == 2);
    REQUIRE(protoMessageId(BodyType::Biped) == 400);
    REQUIRE(protoMessageId(BodyType::Robotic) == 402);
}

TEST_CASE("Perk enum mirrors Fallout 2 global perk ids", "[engine][enum]") {
    using namespace geck::fallout;

    REQUIRE(enumValue(PerkId::WeaponLongRange) == 58);
    REQUIRE(enumValue(PerkId::WeaponFlameboy) == 67);
    REQUIRE(enumValue(PerkId::ArmorAdvancedI) == 68);
    REQUIRE(enumValue(PerkId::ArmorAdvancedII) == 69);
    REQUIRE(enumValue(PerkId::JetAddiction) == 70);
    REQUIRE(enumValue(PerkId::TragicAddiction) == 71);
    REQUIRE(enumValue(PerkId::ArmorCharisma) == 72);
    REQUIRE(enumValue(PerkId::WeaponHandling) == 106);
    REQUIRE(enumValue(PerkId::WeaponEnhancedKnockout) == 117);
    REQUIRE(enumValue(PerkId::Jinxed) == 118);

    REQUIRE(perkNameMessageId(PerkId::WeaponLongRange) == 159);
    REQUIRE(perkNameMessageId(PerkId::ArmorCharisma) == 173);
    REQUIRE(perkNameMessageId(PerkId::WeaponEnhancedKnockout) == 218);
    REQUIRE(perkDescriptionMessageId(PerkId::WeaponLongRange) == 1159);
}

TEST_CASE("Weapon and armor proto perk subsets are filtered from the full perk enum", "[engine][enum]") {
    using namespace geck::fallout;

    REQUIRE(WeaponItemPerks.size() == 9);
    REQUIRE(WeaponItemPerks.front() == PerkId::WeaponLongRange);
    REQUIRE(WeaponItemPerks.back() == PerkId::WeaponEnhancedKnockout);

    REQUIRE(ArmorItemPerks.size() == 5);
    REQUIRE(ArmorItemPerks.front() == PerkId::PoweredArmor);
    REQUIRE(ArmorItemPerks.back() == PerkId::ArmorCharisma);

    REQUIRE(NoItemPerk == -1);
    REQUIRE(statNameMessageId(StatId::RadiationResistance) == 131);
    REQUIRE(statNameMessageId(StatId::PoisonResistance) == 132);
}
