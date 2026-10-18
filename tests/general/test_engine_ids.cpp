#include <catch2/catch_test_macros.hpp>

#include "format/frm/FrmId.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "format/pro/ProtoTypes.h"
#include "resource/CritterFrmResolver.h"
#include "util/FalloutEngineEnums.h"

using namespace geck;

// The typed ids are constexpr so that named engine ids stay compile-time constants.
static_assert(ProtoId(MiscProtoTypeId::ScrollBlocker).pid() == 0x0500000Cu);
static_assert(ProtoId(MiscProtoTypeId::FirstExitGrid).pid() == 0x05000010u);
static_assert(ProtoId(CritterProtoTypeId::Dude).pid() == 0x01000000u);
static_assert(FrmId(MiscFrameId::ScrollBlocker).fid() == 0x0500000Cu);
static_assert(FrmId(InterfaceFrameId::ExitGridMarker).fid() == 0x06000003u);

// Proto enums and their proto.msg ids mirror the engine (proto_types.h, proto.cc).
namespace engine_enums {
using fallout::BodyType;
using fallout::CaliberType;
using fallout::DamageType;
using fallout::enumValue;
using fallout::MaterialType;
using fallout::protoMessageId;

static_assert(enumValue(ItemType::Armor) == 0);
static_assert(enumValue(ItemType::Ammo) == 4);
static_assert(protoMessageId(ItemType::Armor) == 150);
static_assert(protoMessageId(ItemType::Ammo) == 154);
// SCENERY_TYPE_LADDER_UP / _DOWN: gecko used to call these LADDER_BOTTOM / _TOP.
static_assert(enumValue(SceneryType::LadderUp) == 3);
static_assert(enumValue(SceneryType::LadderDown) == 4);
static_assert(enumValue(MaterialType::Glass) == 0);
static_assert(enumValue(MaterialType::Leather) == 7);
static_assert(protoMessageId(MaterialType::Glass) == 100);
static_assert(protoMessageId(MaterialType::Leather) == 107);
static_assert(enumValue(DamageType::Normal) == 0);
static_assert(enumValue(DamageType::Explosion) == 6);
static_assert(protoMessageId(DamageType::Normal) == 250);
static_assert(protoMessageId(DamageType::Explosion) == 256);
static_assert(enumValue(CaliberType::None) == 0);
static_assert(enumValue(CaliberType::Mm10) == 8);
static_assert(enumValue(CaliberType::Mm7_62) == 18);
static_assert(protoMessageId(CaliberType::Mm10) == 308);
static_assert(protoMessageId(CaliberType::Mm7_62) == 318);
static_assert(fallout::enumCount<CaliberType>() == 19);
static_assert(enumValue(BodyType::Biped) == 0);
static_assert(enumValue(BodyType::Robotic) == 2);
static_assert(protoMessageId(BodyType::Biped) == 400);
static_assert(protoMessageId(BodyType::Robotic) == 402);

// Global perk ids (perk_defs.h) and their perk.msg / stat.msg name ids.
using fallout::PerkId;
static_assert(enumValue(PerkId::WeaponLongRange) == 58);
static_assert(enumValue(PerkId::WeaponFlameboy) == 67);
static_assert(enumValue(PerkId::ArmorAdvancedI) == 68);
static_assert(enumValue(PerkId::ArmorAdvancedII) == 69);
static_assert(enumValue(PerkId::JetAddiction) == 70);
static_assert(enumValue(PerkId::TragicAddiction) == 71);
static_assert(enumValue(PerkId::ArmorCharisma) == 72);
static_assert(enumValue(PerkId::WeaponHandling) == 106);
static_assert(enumValue(PerkId::WeaponEnhancedKnockout) == 117);
static_assert(enumValue(PerkId::Jinxed) == 118);
static_assert(fallout::perkNameMessageId(PerkId::WeaponLongRange) == 159);
static_assert(fallout::perkNameMessageId(PerkId::ArmorCharisma) == 173);
static_assert(fallout::perkNameMessageId(PerkId::WeaponEnhancedKnockout) == 218);
static_assert(fallout::statNameMessageId(fallout::StatId::RadiationResistance) == 131);
static_assert(fallout::statNameMessageId(fallout::StatId::PoisonResistance) == 132);

// The weapon and armour proto perk subsets are filtered from the full perk enum.
static_assert(fallout::WEAPON_ITEM_PERKS.size() == 9);
static_assert(fallout::WEAPON_ITEM_PERKS.front() == PerkId::WeaponLongRange);
static_assert(fallout::WEAPON_ITEM_PERKS.back() == PerkId::WeaponEnhancedKnockout);
static_assert(fallout::ARMOR_ITEM_PERKS.size() == 5);
static_assert(fallout::ARMOR_ITEM_PERKS.front() == PerkId::PoweredArmor);
static_assert(fallout::ARMOR_ITEM_PERKS.back() == PerkId::ArmorCharisma);
static_assert(fallout::NO_ITEM_PERK == -1);
} // namespace engine_enums

TEST_CASE("ProtoId decodes a PID like the engine", "[engine][ids]") {
    const ProtoId scrub{ 0x02000066u };
    CHECK(scrub.objectType() == ObjectType::Scenery);
    CHECK(scrub.protoId() == 0x66u);
    CHECK(scrub.valid());
    CHECK(ProtoId(ObjectType::Scenery, 0x66) == scrub);

    CHECK(ProtoId(0x0500000Cu) == MiscProtoTypeId::ScrollBlocker);
    CHECK_FALSE(ProtoId(0x0500000Du) == MiscProtoTypeId::ScrollBlocker);
}

TEST_CASE("ProtoId keeps a PID with no known type losslessly", "[engine][ids]") {
    // RPU's epamain1/epamain2 carry pid -1 records; the raw value must survive untouched.
    const ProtoId none{ 0xFFFFFFFFu };
    CHECK(none.pid() == 0xFFFFFFFFu);
    CHECK(none.objectType() == ObjectType::Invalid);
    CHECK_FALSE(none.hasPid());
    CHECK_FALSE(none.valid());
    CHECK(none == ProtoId{ });

    // Art-only types (interface and up) are not proto types.
    CHECK(ProtoId(0x06000001u).objectType() == ObjectType::Invalid);
}

TEST_CASE("Pro::type reads the PID's whole high byte, as the engine does", "[engine][ids][pro]") {
    // Pro::type() used to mask with 0x0F000000, so it disagreed with every other PID decoder (and
    // the engine's `pid >> 24`) whenever the top nibble was set: pid -1 read as type 15.
    Pro pro{ "test.pro" };
    pro.header.PID = -1;
    CHECK(pro.type() == ObjectType::Invalid);
    CHECK(pro.type() == ProtoId(static_cast<uint32_t>(pro.header.PID)).objectType());

    pro.header.PID = 0x10000005; // the type nibble alone would say ITEM
    CHECK(pro.type() == ObjectType::Invalid);

    pro.header.PID = 0x03000005;
    CHECK(pro.type() == ObjectType::Wall);
}

TEST_CASE("MapObject exposes typed ids and classifies by them", "[engine][ids][map]") {
    MapObject object;
    object.pro_pid = ProtoId(MiscProtoTypeId::ScrollBlocker).pid();
    object.frm_pid = FrmId(MiscFrameId::Block).fid();
    CHECK(object.objectType() == ObjectType::Misc);
    CHECK(object.isScrollBlocker());
    CHECK_FALSE(object.isExitGridMarker());

    object.pro_pid = ProtoId(MiscProtoTypeId::LastExitGrid).pid();
    CHECK(object.isExitGridMarker());
    object.pro_pid = ProtoId(ObjectType::Misc, static_cast<uint32_t>(MiscProtoTypeId::LastExitGrid) + 1).pid();
    CHECK_FALSE(object.isExitGridMarker());
}

TEST_CASE("FrmId decodes every FID field", "[engine][ids]") {
    // rotation 2 | critter | animation 0x0B | weapon 0x3 | frame 0x123
    const FrmId fid{ 0x210B3123u };
    CHECK(fid.objectType() == ObjectType::Critter); // rotation bits do not leak into the type
    CHECK(fid.rotation() == Rotation::SE);
    CHECK(fid.animationType() == 0x0Bu);
    CHECK(fid.weaponAnimation() == 0x3u);
    CHECK(fid.frameId() == 0x123u);
    CHECK(FrmId(ObjectType::Critter, 0x123, 0x0B, 0x3, Rotation::SE) == fid);

    // Rotation bits past NW name no facing.
    CHECK(FrmId(0x71000000u).rotation() == Rotation::Invalid);
    // Type nibbles past SkillDex name no art.
    CHECK(FrmId(0x0F000000u).objectType() == ObjectType::Invalid);
}

TEST_CASE("FrmId's frame id is the engine's 12-bit lst index", "[engine][ids]") {
    // fallout2-ce art.cc reads `fid & 0xFFF` for every type; bits 12-23 are animation codes.
    CHECK(FrmId(0x05001001u).frameId() == 1u);
    CHECK(FrmId(ObjectType::Tile, 0x1234).fid() == 0x04000234u);
}

TEST_CASE("Critter FID names round-trip through FrmId", "[engine][ids][critter]") {
    const uint32_t multi = CritterFrmResolver::deriveCritterFrmPid("hmjmpsaa.frm", 7);
    CHECK(FrmId(multi) == FrmId(ObjectType::Critter, 7, 0, 0, Rotation::NE));
    CHECK(CritterFrmResolver::generateCritterFrmName("hmjmps", multi) == "hmjmpsaa.frm");

    // Engine art.cc: rotation bits r > 0 name the single-direction file .fr(r-1).
    const uint32_t single = CritterFrmResolver::deriveCritterFrmPid("hmjmpsab.fr2", 7);
    CHECK(FrmId(single).rotation() == Rotation::SW);
    CHECK(CritterFrmResolver::generateCritterFrmName("hmjmps", single) == "hmjmpsab.fr2");
}

TEST_CASE("Map header elevation flags name the engine bits", "[engine][ids][map]") {
    CHECK(Map::elevationFlag(0) == static_cast<uint32_t>(MapHeaderFlags::Elevation0));
    CHECK(Map::elevationFlag(2) == static_cast<uint32_t>(MapHeaderFlags::Elevation2));
    const uint32_t onlyMiddle = static_cast<uint32_t>(MapHeaderFlags::Elevation0) | static_cast<uint32_t>(MapHeaderFlags::Elevation2);
    CHECK_FALSE(Map::elevationIsPresent(onlyMiddle, 0));
    CHECK(Map::elevationIsPresent(onlyMiddle, 1));
    CHECK_FALSE(Map::elevationIsPresent(onlyMiddle, 2));
}
