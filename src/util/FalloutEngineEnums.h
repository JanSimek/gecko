#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace geck::fallout {

template <typename Enum>
constexpr int enumValue(Enum value) {
    static_assert(std::is_enum_v<Enum>, "Enum value expected");
    return static_cast<int>(value);
}

template <typename Enum>
constexpr size_t enumCount() {
    static_assert(std::is_enum_v<Enum>, "Enum type expected");
    return static_cast<size_t>(Enum::Count);
}

inline constexpr int NO_ITEM_PERK = -1;
inline constexpr uint32_t PROTO_ID_MONEY = 41;

inline constexpr int PROTO_MATERIAL_MESSAGE_ID_BASE = 100;
inline constexpr int PROTO_ITEM_TYPE_MESSAGE_ID_BASE = 150;
inline constexpr int PROTO_SCENERY_TYPE_MESSAGE_ID_BASE = 200;
inline constexpr int PROTO_DAMAGE_TYPE_MESSAGE_ID_BASE = 250;
inline constexpr int PROTO_CALIBER_MESSAGE_ID_BASE = 300;
inline constexpr int PROTO_BODY_TYPE_MESSAGE_ID_BASE = 400;

inline constexpr int STAT_NAME_MESSAGE_ID_BASE = 100;
inline constexpr int PERK_NAME_MESSAGE_ID_BASE = 101;
inline constexpr int PERK_DESCRIPTION_MESSAGE_ID_BASE = 1101;

enum class Gender : int {
    Male = 0,
    Female,
    Count,
};

enum class ItemType : int {
    Armor = 0,
    Container,
    Drug,
    Weapon,
    Ammo,
    Misc,
    Key,
    Count,
};

enum class SceneryType : int {
    Door = 0,
    Stairs,
    Elevator,
    LadderUp,
    LadderDown,
    Generic,
    Count,
};

enum class MaterialType : int {
    Glass = 0,
    Metal,
    Plastic,
    Wood,
    Dirt,
    Stone,
    Cement,
    Leather,
    Count,
};

enum class DamageType : int {
    Normal = 0,
    Laser,
    Fire,
    Plasma,
    Electrical,
    Emp,
    Explosion,
    Count,
};

enum class CaliberType : int {
    None = 0,
    Rocket,
    FlamethrowerFuel,
    CEnergyCell,
    DEnergyCell,
    Cal223,
    Mm5,
    Cal40,
    Mm10,
    Cal44,
    Mm14,
    Gauge12,
    Mm9,
    Bb,
    Cal45,
    Mm2,
    Mm4_7Caseless,
    NhNeedler,
    Mm7_62,
    Count,
};

enum class BodyType : int {
    Biped = 0,
    Quadruped,
    Robotic,
    Count,
};

enum class StatId : int {
    Strength = 0,
    Perception,
    Endurance,
    Charisma,
    Intelligence,
    Agility,
    Luck,
    MaximumHitPoints,
    MaximumActionPoints,
    ArmorClass,
    UnarmedDamage,
    MeleeDamage,
    CarryWeight,
    Sequence,
    HealingRate,
    CriticalChance,
    BetterCriticals,
    DamageThreshold,
    DamageThresholdLaser,
    DamageThresholdFire,
    DamageThresholdPlasma,
    DamageThresholdElectrical,
    DamageThresholdEmp,
    DamageThresholdExplosion,
    DamageResistance,
    DamageResistanceLaser,
    DamageResistanceFire,
    DamageResistancePlasma,
    DamageResistanceElectrical,
    DamageResistanceEmp,
    DamageResistanceExplosion,
    RadiationResistance,
    PoisonResistance,
    Age,
    Gender,
    CurrentHitPoints,
    CurrentPoisonLevel,
    CurrentRadiationLevel,
    Count,
};

enum class PerkId : int {
    Awareness = 0,
    BonusHthAttacks,
    BonusHthDamage,
    BonusMove,
    BonusRangedDamage,
    BonusRateOfFire,
    EarlierSequence,
    FasterHealing,
    MoreCriticals,
    NightVision,
    Presence,
    RadResistance,
    Toughness,
    StrongBack,
    Sharpshooter,
    SilentRunning,
    Survivalist,
    MasterTrader,
    Educated,
    Healer,
    FortuneFinder,
    BetterCriticals,
    Empathy,
    Slayer,
    Sniper,
    SilentDeath,
    ActionBoy,
    MentalBlock,
    Lifegiver,
    Dodger,
    Snakeater,
    MrFixit,
    Medic,
    MasterThief,
    Speaker,
    HeaveHo,
    FriendlyFoe,
    Pickpocket,
    Ghost,
    CultOfPersonality,
    Scrounger,
    Explorer,
    FlowerChild,
    Pathfinder,
    AnimalFriend,
    Scout,
    MysteriousStranger,
    Ranger,
    QuickPockets,
    SmoothTalker,
    SwiftLearner,
    Tag,
    Mutate,
    NukaColaAddiction,
    BuffoutAddiction,
    MentatsAddiction,
    PsychoAddiction,
    RadawayAddiction,
    WeaponLongRange,
    WeaponAccurate,
    WeaponPenetrate,
    WeaponKnockback,
    PoweredArmor,
    CombatArmor,
    WeaponScopeRange,
    WeaponFastReload,
    WeaponNightSight,
    WeaponFlameboy,
    ArmorAdvancedI,
    ArmorAdvancedII,
    JetAddiction,
    TragicAddiction,
    ArmorCharisma,
    GeckoSkinning,
    DermalImpactArmor,
    DermalImpactAssaultEnhancement,
    PhoenixArmorImplants,
    PhoenixAssaultEnhancement,
    VaultCityInoculations,
    AdrenalineRush,
    CautiousNature,
    Comprehension,
    DemolitionExpert,
    Gambler,
    GainStrength,
    GainPerception,
    GainEndurance,
    GainCharisma,
    GainIntelligence,
    GainAgility,
    GainLuck,
    Harmless,
    HereAndNow,
    HthEvade,
    KamaSutraMaster,
    KarmaBeacon,
    LightStep,
    LivingAnatomy,
    MagneticPersonality,
    Negotiator,
    PackRat,
    Pyromaniac,
    QuickRecovery,
    Salesman,
    Stonewall,
    Thief,
    WeaponHandling,
    VaultCityTraining,
    AlcoholRaisedHitPoints,
    AlcoholRaisedHitPointsII,
    AlcoholLoweredHitPoints,
    AlcoholLoweredHitPointsII,
    AutodocRaisedHitPoints,
    AutodocRaisedHitPointsII,
    AutodocLoweredHitPoints,
    AutodocLoweredHitPointsII,
    ExpertExcrementExpeditor,
    WeaponEnhancedKnockout,
    Jinxed,
    Count,
};

constexpr int protoMessageId(ItemType itemType) {
    return PROTO_ITEM_TYPE_MESSAGE_ID_BASE + enumValue(itemType);
}

constexpr int protoMessageId(SceneryType sceneryType) {
    return PROTO_SCENERY_TYPE_MESSAGE_ID_BASE + enumValue(sceneryType);
}

constexpr int protoMessageId(MaterialType materialType) {
    return PROTO_MATERIAL_MESSAGE_ID_BASE + enumValue(materialType);
}

constexpr int protoMessageId(DamageType damageType) {
    return PROTO_DAMAGE_TYPE_MESSAGE_ID_BASE + enumValue(damageType);
}

constexpr int protoMessageId(CaliberType caliberType) {
    return PROTO_CALIBER_MESSAGE_ID_BASE + enumValue(caliberType);
}

constexpr int protoMessageId(BodyType bodyType) {
    return PROTO_BODY_TYPE_MESSAGE_ID_BASE + enumValue(bodyType);
}

constexpr int statNameMessageId(StatId stat) {
    return STAT_NAME_MESSAGE_ID_BASE + enumValue(stat);
}

constexpr int perkNameMessageId(PerkId perk) {
    return PERK_NAME_MESSAGE_ID_BASE + enumValue(perk);
}

constexpr int perkDescriptionMessageId(PerkId perk) {
    return PERK_DESCRIPTION_MESSAGE_ID_BASE + enumValue(perk);
}

inline constexpr std::array<PerkId, 9> WEAPON_ITEM_PERKS = {
    PerkId::WeaponLongRange,
    PerkId::WeaponAccurate,
    PerkId::WeaponPenetrate,
    PerkId::WeaponKnockback,
    PerkId::WeaponScopeRange,
    PerkId::WeaponFastReload,
    PerkId::WeaponNightSight,
    PerkId::WeaponFlameboy,
    PerkId::WeaponEnhancedKnockout,
};

inline constexpr std::array<PerkId, 5> ARMOR_ITEM_PERKS = {
    PerkId::PoweredArmor,
    PerkId::CombatArmor,
    PerkId::ArmorAdvancedI,
    PerkId::ArmorAdvancedII,
    PerkId::ArmorCharisma,
};

} // namespace geck::fallout
