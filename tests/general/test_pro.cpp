#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "support/ByteWriter.h"
#include "support/EngineProtoBytes.h"
#include "support/Fixtures.h"
#include "support/ProBuilder.h"
#include "support/TempFile.h"

using namespace geck::test;

TEST_CASE("ProReader reads the Rad-X drug fixture's item and drug fields", "[pro]") {
    geck::ProReader pro_reader{};
    auto pro_file = pro_reader.openFile(geck::test::dataPath("test_item_drug_radx.pro"));

    // Verify it's an item type
    REQUIRE(pro_file->type() == geck::ObjectType::Item);
    REQUIRE(pro_file->itemType() == geck::ItemType::Drug);

    // Verify header fields
    REQUIRE(pro_file->header.message_id == 10900);

    // Verify common item data
    REQUIRE(pro_file->commonItemData.weight == 0);
    REQUIRE(pro_file->commonItemData.basePrice == 300);
    REQUIRE(pro_file->commonItemData.SID == 0xffffffff);
    REQUIRE(pro_file->commonItemData.materialId == 1);
    REQUIRE(pro_file->commonItemData.containerSize == 0);
    REQUIRE(pro_file->commonItemData.inventoryFID == 0x700002b);
    REQUIRE(pro_file->commonItemData.soundId == 48); // ASCII '0'

    // The whole flags words, so a stray or missing bit fails: flat, light- and shoot-through; the
    // item's extended action bits.
    using ObjectFlags = geck::Pro::ObjectFlags;
    using ExtendedItemFlags = geck::Pro::ExtendedItemFlags;
    CHECK(pro_file->header.flags
        == (static_cast<uint32_t>(ObjectFlags::OBJECT_FLAT) | static_cast<uint32_t>(ObjectFlags::OBJECT_LIGHT_THRU)
            | static_cast<uint32_t>(ObjectFlags::OBJECT_SHOOT_THRU)));
    CHECK(pro_file->commonItemData.flagsExt
        == (static_cast<uint32_t>(ExtendedItemFlags::CAN_USE_ON) | static_cast<uint32_t>(ExtendedItemFlags::GENERAL_FLAG)
            | static_cast<uint32_t>(ExtendedItemFlags::INTERACTION_FLAG)));

    // Verify drug-specific data fields
    // The drug data structure contains immediate and delayed effects
    REQUIRE(pro_file->drugData.stat0 == 31); // Radiation resistance
    REQUIRE(pro_file->drugData.stat1 == 0xffffffff);
    REQUIRE(pro_file->drugData.stat2 == 0xffffffff);

    REQUIRE(pro_file->drugData.amount0 == 50);
    REQUIRE(pro_file->drugData.amount1 == 0);
    REQUIRE(pro_file->drugData.amount2 == 0);

    REQUIRE(pro_file->drugData.duration1 == 1440);
    REQUIRE(pro_file->drugData.amount0_1 == -25);
    REQUIRE(pro_file->drugData.amount1_1 == 0);
    REQUIRE(pro_file->drugData.amount2_1 == 0);

    REQUIRE(pro_file->drugData.duration2 == 2880);
    REQUIRE(pro_file->drugData.amount0_2 == -25);
    REQUIRE(pro_file->drugData.amount1_2 == 0);
    REQUIRE(pro_file->drugData.amount2_2 == 0);

    REQUIRE(pro_file->drugData.addictionRate == 0);
    REQUIRE(pro_file->drugData.addictionEffect == 0xffffffff);
    REQUIRE(pro_file->drugData.addictionOnset == 0);
}

// ---------------------------------------------------------------------------
// Level 1: generic read -> write -> read integrity using the shipped fixture.
// Proves that a real .pro parsed by ProReader, serialized by ProWriter, and
// re-parsed produces identical state AND identical on-disk bytes.
// ---------------------------------------------------------------------------
TEST_CASE("PRO generic round-trip preserves bytes and state", "[pro][roundtrip]") {
    const std::filesystem::path fixture = dataPath("test_item_drug_radx.pro");

    geck::ProReader reader{};
    auto original = reader.openFile(fixture);
    REQUIRE(original != nullptr);
    REQUIRE(original->type() == geck::ObjectType::Item);
    REQUIRE(original->itemType() == geck::ItemType::Drug);

    const TempFile rewritten{ "test_pro_roundtrip_drug", ".pro" };
    const auto& tempPath = rewritten.path();
    writePro(tempPath, *original);

    // On-disk bytes must be identical to the source fixture.
    const auto originalBytes = readAllBytes(fixture);
    const auto rewrittenBytes = readAllBytes(tempPath);
    REQUIRE(rewrittenBytes == originalBytes);

    // Re-parse the rewritten file and compare the fields that matter for this
    // object type. This guards against a writer that happens to round-trip
    // bytes by luck but a reader that reinterprets them differently.
    geck::ProReader reader2{};
    auto reparsed = reader2.openFile(tempPath);
    REQUIRE(reparsed != nullptr);

    REQUIRE(reparsed->type() == original->type());
    REQUIRE(reparsed->itemType() == original->itemType());

    REQUIRE(reparsed->header.PID == original->header.PID);
    REQUIRE(reparsed->header.message_id == original->header.message_id);
    REQUIRE(reparsed->header.FID == original->header.FID);
    REQUIRE(reparsed->header.light_distance == original->header.light_distance);
    REQUIRE(reparsed->header.light_intensity == original->header.light_intensity);
    REQUIRE(reparsed->header.flags == original->header.flags);

    REQUIRE(reparsed->commonItemData.flagsExt == original->commonItemData.flagsExt);
    REQUIRE(reparsed->commonItemData.SID == original->commonItemData.SID);
    REQUIRE(reparsed->commonItemData.materialId == original->commonItemData.materialId);
    REQUIRE(reparsed->commonItemData.containerSize == original->commonItemData.containerSize);
    REQUIRE(reparsed->commonItemData.weight == original->commonItemData.weight);
    REQUIRE(reparsed->commonItemData.basePrice == original->commonItemData.basePrice);
    REQUIRE(reparsed->commonItemData.inventoryFID == original->commonItemData.inventoryFID);
    REQUIRE(reparsed->commonItemData.soundId == original->commonItemData.soundId);

    REQUIRE(reparsed->drugData.stat0 == original->drugData.stat0);
    REQUIRE(reparsed->drugData.stat1 == original->drugData.stat1);
    REQUIRE(reparsed->drugData.stat2 == original->drugData.stat2);
    REQUIRE(reparsed->drugData.amount0 == original->drugData.amount0);
    REQUIRE(reparsed->drugData.amount1 == original->drugData.amount1);
    REQUIRE(reparsed->drugData.amount2 == original->drugData.amount2);
    REQUIRE(reparsed->drugData.duration1 == original->drugData.duration1);
    REQUIRE(reparsed->drugData.amount0_1 == original->drugData.amount0_1);
    REQUIRE(reparsed->drugData.amount1_1 == original->drugData.amount1_1);
    REQUIRE(reparsed->drugData.amount2_1 == original->drugData.amount2_1);
    REQUIRE(reparsed->drugData.duration2 == original->drugData.duration2);
    REQUIRE(reparsed->drugData.amount0_2 == original->drugData.amount0_2);
    REQUIRE(reparsed->drugData.amount1_2 == original->drugData.amount1_2);
    REQUIRE(reparsed->drugData.amount2_2 == original->drugData.amount2_2);
    REQUIRE(reparsed->drugData.addictionRate == original->drugData.addictionRate);
    REQUIRE(reparsed->drugData.addictionEffect == original->drugData.addictionEffect);
    REQUIRE(reparsed->drugData.addictionOnset == original->drugData.addictionOnset);
}

// ---------------------------------------------------------------------------
// Level 2: wall flagsExt/SID guard. ProWriter::writeWallData previously serialized hard
// zeros for the extended flags and SID fields (writeBE32(0)), silently losing
// any non-zero engine values. Wall .pro files are derived purely from the PID
// type byte (Pro::type() == ProtoId::objectType(), the PID's high byte), so a faithful wall
// prototype can be constructed entirely in memory without a fixture.
//
// We set flagsExt and SID to distinctive non-zero sentinels, write, read back,
// and assert they survive. Against the old writeBE32(0) code these reads return
// 0 and the test FAILS; against the fix they round-trip and the test PASSES.
// ---------------------------------------------------------------------------
TEST_CASE("PRO wall round-trip preserves flagsExt and SID", "[pro][roundtrip][wall]") {
    // PID type byte == 3 selects ObjectType::Wall. The remaining bits are
    // an arbitrary but distinctive prototype index, preserved verbatim.
    constexpr int32_t WALL_PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Wall, 0x00000005u).pid());

    constexpr uint32_t SENTINEL_FLAGS_EXT = 0xABCD1234u;
    constexpr uint32_t SENTINEL_SID = 0x42u;
    constexpr uint32_t SENTINEL_MATERIAL = 0x07u;

    geck::Pro wall{ "wall.pro" };
    wall.header.PID = WALL_PID;
    wall.header.message_id = 1234;
    wall.header.FID = 0x04000010;
    wall.header.light_distance = 8;
    wall.header.light_intensity = 65536;
    wall.header.flags = 0x00000020; // arbitrary distinctive header flags
    wall.commonItemData.flagsExt = SENTINEL_FLAGS_EXT;
    wall.commonItemData.SID = SENTINEL_SID;
    wall.wallData.materialId = SENTINEL_MATERIAL;

    REQUIRE(wall.type() == geck::ObjectType::Wall);

    const geck::Pro got = proRoundTrip(wall).pro;
    REQUIRE(got.type() == geck::ObjectType::Wall);

    // Header integrity.
    REQUIRE(got.header.PID == WALL_PID);
    REQUIRE(got.header.message_id == 1234);
    REQUIRE(got.header.FID == 0x04000010);
    REQUIRE(got.header.light_distance == 8);
    REQUIRE(got.header.light_intensity == 65536);
    REQUIRE(got.header.flags == 0x00000020);

    // The actual wall-fix regression assertions: these are the fields the old
    // writer zeroed out.
    REQUIRE(got.commonItemData.flagsExt == SENTINEL_FLAGS_EXT);
    REQUIRE(got.commonItemData.SID == SENTINEL_SID);
    REQUIRE(got.wallData.materialId == SENTINEL_MATERIAL);
}

// ---------------------------------------------------------------------------
// Level 3: per-type read->write->read for the non-item object types, built in
// memory (type comes from the PID nibble). These guard the reader/writer field
// schema against drift - notably that CRITTER and SCENERY write the common
// flagsExt/SID prefix that the reader reads (writeCritterData/writeSceneryData
// previously omitted them, corrupting saved critter/scenery prototypes).
// ---------------------------------------------------------------------------

TEST_CASE("PRO critter round-trip preserves the common header and stats", "[pro][roundtrip][critter]") {
    geck::Pro critter{ "critter.pro" };
    critter.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Critter, 0x42u).pid());
    critter.header.message_id = 111;
    critter.header.FID = 0x01000005;
    critter.header.light_distance = 6;
    critter.header.light_intensity = 65536;
    critter.header.flags = 0x00000040;
    critter.commonItemData.flagsExt = 0xDEADBEEFu; // <- bug fields: writer must emit these
    critter.commonItemData.SID = 0x1234u;

    auto& c = critter.critterData;
    c.headFID = 700;
    c.aiPacket = 701;
    c.teamNumber = 702;
    c.flags = 703;
    for (int i = 0; i < geck::Pro::SPECIAL_STATS_COUNT; ++i)
        c.specialStats[i] = 10 + i;
    c.maxHitPoints = 50;
    c.experienceForKill = 999;
    c.killType = 3;
    c.damageType = 2;
    c.bodyType = 1;

    REQUIRE(critter.type() == geck::ObjectType::Critter);

    const geck::Pro got = proRoundTrip(critter).pro;

    REQUIRE(got.type() == geck::ObjectType::Critter);
    REQUIRE(got.header.PID == critter.header.PID);
    REQUIRE(got.header.flags == critter.header.flags);
    // The regression: these survive only if the writer emits the common prefix.
    REQUIRE(got.commonItemData.flagsExt == 0xDEADBEEFu);
    REQUIRE(got.commonItemData.SID == 0x1234u);
    REQUIRE(got.critterData.headFID == 700);
    REQUIRE(got.critterData.aiPacket == 701);
    REQUIRE(got.critterData.maxHitPoints == 50);
    REQUIRE(got.critterData.experienceForKill == 999);
    REQUIRE(got.critterData.killType == 3);
    REQUIRE(got.critterData.damageType == 2);
}

TEST_CASE("PRO scenery round-trip preserves the common header and subtype data", "[pro][roundtrip][scenery]") {
    geck::Pro scenery{ "scenery.pro" };
    scenery.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Scenery, 0x10u).pid());
    scenery.header.message_id = 222;
    scenery.header.FID = 0x02000003;
    scenery.header.flags = 0x00000080;
    scenery.commonItemData.flagsExt = 0xCAFEF00Du; // <- bug fields
    scenery.commonItemData.SID = 0x5678u;
    scenery.setObjectSubtypeId(static_cast<unsigned int>(geck::SceneryType::Stairs));
    scenery.sceneryData.materialId = 4;
    scenery.sceneryData.soundId = 9;
    scenery.sceneryData.stairsData.destTile = 12345;
    scenery.sceneryData.stairsData.destElevation = 2;

    REQUIRE(scenery.type() == geck::ObjectType::Scenery);

    const geck::Pro got = proRoundTrip(scenery).pro;

    REQUIRE(got.type() == geck::ObjectType::Scenery);
    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::SceneryType::Stairs));
    REQUIRE(got.commonItemData.flagsExt == 0xCAFEF00Du);
    REQUIRE(got.commonItemData.SID == 0x5678u);
    REQUIRE(got.sceneryData.materialId == 4);
    REQUIRE(got.sceneryData.soundId == 9);
    REQUIRE(got.sceneryData.stairsData.destTile == 12345);
    REQUIRE(got.sceneryData.stairsData.destElevation == 2);
}

TEST_CASE("PRO weapon round-trip keeps sfall's energy bit in the extended flags", "[pro][roundtrip][weapon]") {
    using enum geck::Pro::ExtendedItemFlags;
    const uint32_t flagsExt = static_cast<uint32_t>(ENERGY) | static_cast<uint32_t>(BIG_GUN) | 0x76u;

    geck::Pro weapon{ "weapon.pro" };
    weapon.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Item, 0x20u).pid());
    weapon.header.flags = 0x00000008;
    weapon.commonItemData.flagsExt = flagsExt;
    weapon.commonItemData.SID = 0x33u;
    weapon.commonItemData.weight = 5;
    weapon.commonItemData.basePrice = 250;
    weapon.commonItemData.soundId = 7;
    weapon.setObjectSubtypeId(static_cast<unsigned int>(geck::ItemType::Weapon));
    weapon.weaponData.damageMin = 8;
    weapon.weaponData.damageMax = 16;
    weapon.weaponData.ammoPID = 0x00000029;
    weapon.weaponData.ammoCapacity = 30;
    weapon.weaponData.soundId = 4;

    const ProRoundTrip result = proRoundTrip(weapon);
    const geck::Pro& got = result.pro;

    // fallout2-ce protoItemDataRead stops at the weapon's sound byte: nothing trails it.
    REQUIRE(result.fileSize == 122);
    REQUIRE(got.itemType() == geck::ItemType::Weapon);
    REQUIRE(got.commonItemData.flagsExt == flagsExt);
    REQUIRE(geck::Pro::hasFlag(got.commonItemData.flagsExt, ENERGY));
    REQUIRE(got.commonItemData.SID == 0x33u);
    REQUIRE(got.commonItemData.weight == 5);
    REQUIRE(got.weaponData.damageMin == 8);
    REQUIRE(got.weaponData.damageMax == 16);
    REQUIRE(got.weaponData.ammoPID == 0x00000029);
    REQUIRE(got.weaponData.ammoCapacity == 30);
    REQUIRE(got.weaponData.soundId == 4);
}

TEST_CASE("PRO weapon rewrites the engine's 122-byte record byte for byte and ignores trailing bytes", "[pro][roundtrip][weapon]") {
    // weaponProto lays the record out in fallout2-ce protoRead + protoItemDataRead order.
    const ByteWriter engine = weaponProto(0x00000020, 100, 0x00000476u);
    REQUIRE(engine.size() == 122);

    // The engine reads a fixed-size record, so bytes past it are not an error.
    ByteWriter padded = engine;
    padded.be32(0x00000001u);

    TempFile source{ "test_pro_weapon_engine_layout", ".pro" };
    writeBytes(source.path(), padded.data());

    geck::ProReader reader{};
    auto pro = reader.openFile(source.path());
    REQUIRE(pro != nullptr);
    REQUIRE(pro->itemType() == geck::ItemType::Weapon);
    REQUIRE(pro->commonItemData.flagsExt == 0x00000476u);
    REQUIRE(geck::Pro::hasFlag(pro->commonItemData.flagsExt, geck::Pro::ExtendedItemFlags::ENERGY));
    REQUIRE(pro->weaponData.perk == 58u);
    REQUIRE(pro->weaponData.ammoCapacity == 30u);
    REQUIRE(pro->weaponData.soundId == 'A');

    TempFile rewritten{ "test_pro_weapon_engine_layout_out", ".pro" };
    writePro(rewritten.path(), *pro);
    REQUIRE(readAllBytes(rewritten.path()) == engine.data());
}

TEST_CASE("PRO container round-trip preserves item subtype data", "[pro][roundtrip][item]") {
    geck::Pro container{ "container.pro" };
    container.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Item, 0x30u).pid());
    container.commonItemData.flagsExt = 0x44445555u;
    container.commonItemData.SID = 0x66u;
    container.commonItemData.materialId = 2;
    container.setObjectSubtypeId(static_cast<unsigned int>(geck::ItemType::Container));
    container.containerData.maxSize = 100;
    container.containerData.flags = 0x0Au;

    const geck::Pro got = proRoundTrip(container).pro;

    REQUIRE(got.itemType() == geck::ItemType::Container);
    REQUIRE(got.commonItemData.flagsExt == 0x44445555u);
    REQUIRE(got.commonItemData.SID == 0x66u);
    REQUIRE(got.commonItemData.materialId == 2);
    REQUIRE(got.containerData.maxSize == 100);
    REQUIRE(got.containerData.flags == 0x0Au);
}

TEST_CASE("PRO tile round-trip omits the common header prefix", "[pro][roundtrip][tile]") {
    // TILE (and MISC) are the types WITHOUT the flagsExt/SID prefix.
    geck::Pro tile{ "tile.pro" };
    tile.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Tile, 0x55u).pid());
    tile.header.message_id = 321;
    tile.header.flags = 0x00000002;
    tile.tileData.materialId = 3;

    const geck::Pro got = proRoundTrip(tile).pro;

    REQUIRE(got.type() == geck::ObjectType::Tile);
    REQUIRE(got.header.PID == tile.header.PID);
    REQUIRE(got.header.message_id == 321);
    REQUIRE(got.header.flags == 0x00000002);
    REQUIRE(got.tileData.materialId == 3);
}

TEST_CASE("PRO misc round-trip carries no trailing field", "[pro][roundtrip][misc]") {
    // MISC carries no type-specific data: the record ends at flagsExt, matching
    // Fallout 2 CE (proto.cc OBJ_TYPE_MISC reads/writes only through extendedFlags).
    geck::Pro misc{ "misc.pro" };
    misc.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Misc, 0x11u).pid());
    misc.header.message_id = 654;
    misc.header.light_distance = 7;
    misc.header.light_intensity = 0x10000;
    misc.header.flags = 0x00000004;
    misc.commonItemData.flagsExt = 0x0000ABCD;

    const ProRoundTrip result = proRoundTrip(misc);
    const geck::Pro& got = result.pro;

    REQUIRE(got.type() == geck::ObjectType::Misc);
    REQUIRE(got.header.PID == misc.header.PID);
    REQUIRE(got.header.message_id == 654);
    REQUIRE(got.header.light_distance == 7);
    REQUIRE(got.header.light_intensity == 0x10000);
    REQUIRE(got.header.flags == 0x00000004);
    REQUIRE(got.commonItemData.flagsExt == 0x0000ABCD);

    // Exactly 7 BE32 fields (PID, message_id, FID, light_distance, light_intensity,
    // flags, flagsExt) = 28 bytes — not 32 with a phantom trailing field.
    REQUIRE(result.fileSize == 28);
}

// ---------------------------------------------------------------------------
// Level 4: the remaining item subtypes (ARMOR/AMMO/MISC-item/KEY) and scenery
// subtypes (DOOR/ELEVATOR/LADDER/GENERIC). These tails were the ones NOT
// previously round-trip covered. Per-index array sentinels (e.g. 10+i) make the
// test fail on any dropped/added/reordered field, guarding the hand-enumerated
// reader/writer schema without introducing a serialization visitor.
// ---------------------------------------------------------------------------

TEST_CASE("PRO armor round-trip preserves resist/threshold arrays and common data", "[pro][roundtrip][item][armor]") {
    geck::Pro armor{ "armor.pro" };
    armor.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Item, 0x12u).pid());
    armor.commonItemData.flagsExt = 0xA1A2A3A4u;
    armor.commonItemData.SID = 0xB5u;
    armor.commonItemData.materialId = 1;
    armor.commonItemData.weight = 25;
    armor.commonItemData.basePrice = 1000;
    armor.commonItemData.inventoryFID = 0x00000123;
    armor.commonItemData.soundId = 0x2A;
    armor.setObjectSubtypeId(static_cast<unsigned int>(geck::ItemType::Armor));
    armor.armorData.armorClass = 20;
    for (int i = 0; i < geck::Pro::DAMAGE_TYPES_ARMOR; ++i) {
        armor.armorData.damageResist[i] = static_cast<uint32_t>(10 + i); // per-index -> catches array drift
        armor.armorData.damageThreshold[i] = static_cast<uint32_t>(30 + i);
    }
    armor.armorData.perk = 7;
    armor.armorData.armorMaleFID = 0x0100002A;
    armor.armorData.armorFemaleFID = 0x0100002B;

    const geck::Pro got = proRoundTrip(armor).pro;

    REQUIRE(got.itemType() == geck::ItemType::Armor);
    REQUIRE(got.commonItemData.flagsExt == 0xA1A2A3A4u);
    REQUIRE(got.commonItemData.SID == 0xB5u);
    REQUIRE(got.commonItemData.materialId == 1);
    REQUIRE(got.commonItemData.weight == 25);
    REQUIRE(got.commonItemData.basePrice == 1000);
    REQUIRE(got.commonItemData.inventoryFID == 0x00000123);
    REQUIRE(got.commonItemData.soundId == 0x2A);
    REQUIRE(got.armorData.armorClass == 20);
    for (int i = 0; i < geck::Pro::DAMAGE_TYPES_ARMOR; ++i) {
        REQUIRE(got.armorData.damageResist[i] == static_cast<uint32_t>(10 + i));
        REQUIRE(got.armorData.damageThreshold[i] == static_cast<uint32_t>(30 + i));
    }
    REQUIRE(got.armorData.perk == 7);
    REQUIRE(got.armorData.armorMaleFID == 0x0100002A);
    REQUIRE(got.armorData.armorFemaleFID == 0x0100002B);
}

TEST_CASE("PRO ammo round-trip preserves the signed damage modifiers", "[pro][roundtrip][item][ammo]") {
    geck::Pro ammo{ "ammo.pro" };
    ammo.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Item, 0x13u).pid());
    ammo.commonItemData.flagsExt = 0xC1C2C3C4u;
    ammo.commonItemData.SID = 0xD5u;
    ammo.commonItemData.soundId = 0x1F;
    ammo.setObjectSubtypeId(static_cast<unsigned int>(geck::ItemType::Ammo));
    ammo.ammoData.caliber = 4;
    ammo.ammoData.quantity = 50;
    ammo.ammoData.damageModifier = -3; // signed fields must survive
    ammo.ammoData.damageResistModifier = -10;
    ammo.ammoData.damageMultiplier = 2;
    ammo.ammoData.damageTypeModifier = -1;

    const geck::Pro got = proRoundTrip(ammo).pro;

    REQUIRE(got.itemType() == geck::ItemType::Ammo);
    REQUIRE(got.commonItemData.flagsExt == 0xC1C2C3C4u);
    REQUIRE(got.commonItemData.SID == 0xD5u);
    REQUIRE(got.ammoData.caliber == 4);
    REQUIRE(got.ammoData.quantity == 50);
    REQUIRE(got.ammoData.damageModifier == -3);
    REQUIRE(got.ammoData.damageResistModifier == -10);
    REQUIRE(got.ammoData.damageMultiplier == 2);
    REQUIRE(got.ammoData.damageTypeModifier == -1);
}

TEST_CASE("PRO misc-item round-trip preserves powerType and charges", "[pro][roundtrip][item][miscitem]") {
    // ItemType::Misc (a misc *item* with powerType/charges) — distinct from the
    // top-level ObjectType::Misc object covered above.
    geck::Pro miscItem{ "miscitem.pro" };
    miscItem.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Item, 0x14u).pid());
    miscItem.commonItemData.flagsExt = 0xE1E2E3E4u;
    miscItem.commonItemData.SID = 0xF5u;
    miscItem.commonItemData.soundId = 0x0C;
    miscItem.setObjectSubtypeId(static_cast<unsigned int>(geck::ItemType::Misc));
    miscItem.miscData.powerTypePid = 38; // the ammo proto that recharges it
    miscItem.miscData.powerType = 3;     // its caliber
    miscItem.miscData.charges = 42;

    const ProRoundTrip result = proRoundTrip(miscItem);
    const geck::Pro& got = result.pro;

    // fallout2-ce protoItemDataRead reads three words for a misc item and rejects a shorter proto, so
    // the file is the 57-byte item header plus 12.
    REQUIRE(result.fileSize == 69);
    REQUIRE(got.itemType() == geck::ItemType::Misc);
    REQUIRE(got.commonItemData.flagsExt == 0xE1E2E3E4u);
    REQUIRE(got.commonItemData.SID == 0xF5u);
    REQUIRE(got.miscData.powerTypePid == 38);
    REQUIRE(got.miscData.powerType == 3);
    REQUIRE(got.miscData.charges == 42);
}

TEST_CASE("PRO key round-trip preserves keyId", "[pro][roundtrip][item][key]") {
    geck::Pro key{ "key.pro" };
    key.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Item, 0x15u).pid());
    key.commonItemData.flagsExt = 0x10203040u;
    key.commonItemData.SID = 0x50u;
    key.commonItemData.soundId = 0x03;
    key.setObjectSubtypeId(static_cast<unsigned int>(geck::ItemType::Key));
    key.keyData.keyId = 0x0000BEEF;

    const geck::Pro got = proRoundTrip(key).pro;

    REQUIRE(got.itemType() == geck::ItemType::Key);
    REQUIRE(got.commonItemData.flagsExt == 0x10203040u);
    REQUIRE(got.commonItemData.SID == 0x50u);
    REQUIRE(got.keyData.keyId == 0x0000BEEF);
}

TEST_CASE("PRO scenery door round-trip preserves door fields", "[pro][roundtrip][scenery][door]") {
    geck::Pro door{ "door.pro" };
    door.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Scenery, 0x21u).pid());
    door.commonItemData.flagsExt = 0x0A0B0C0Du;
    door.commonItemData.SID = 0x0Eu;
    door.setObjectSubtypeId(static_cast<unsigned int>(geck::SceneryType::Door));
    door.sceneryData.materialId = 5;
    door.sceneryData.soundId = 11;
    door.sceneryData.doorData.walkThroughFlag = 1;
    door.sceneryData.doorData.unknownField = 0x1234;

    const geck::Pro got = proRoundTrip(door).pro;

    REQUIRE(got.type() == geck::ObjectType::Scenery);
    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::SceneryType::Door));
    REQUIRE(got.commonItemData.flagsExt == 0x0A0B0C0Du);
    REQUIRE(got.commonItemData.SID == 0x0Eu);
    REQUIRE(got.sceneryData.materialId == 5);
    REQUIRE(got.sceneryData.soundId == 11);
    REQUIRE(got.sceneryData.doorData.walkThroughFlag == 1);
    REQUIRE(got.sceneryData.doorData.unknownField == 0x1234);
}

TEST_CASE("PRO scenery elevator round-trip preserves elevator fields", "[pro][roundtrip][scenery][elevator]") {
    geck::Pro elevator{ "elevator.pro" };
    elevator.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Scenery, 0x22u).pid());
    elevator.commonItemData.flagsExt = 0x11223344u;
    elevator.commonItemData.SID = 0x55u;
    elevator.setObjectSubtypeId(static_cast<unsigned int>(geck::SceneryType::Elevator));
    elevator.sceneryData.materialId = 6;
    elevator.sceneryData.soundId = 13;
    elevator.sceneryData.elevatorData.elevatorType = 3;
    elevator.sceneryData.elevatorData.elevatorLevel = 2;

    const geck::Pro got = proRoundTrip(elevator).pro;

    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::SceneryType::Elevator));
    REQUIRE(got.commonItemData.flagsExt == 0x11223344u);
    REQUIRE(got.sceneryData.materialId == 6);
    REQUIRE(got.sceneryData.soundId == 13);
    REQUIRE(got.sceneryData.elevatorData.elevatorType == 3);
    REQUIRE(got.sceneryData.elevatorData.elevatorLevel == 2);
}

TEST_CASE("PRO scenery ladder round-trip preserves the packed dest field", "[pro][roundtrip][scenery][ladder]") {
    geck::Pro ladder{ "ladder.pro" };
    ladder.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Scenery, 0x23u).pid());
    ladder.commonItemData.flagsExt = 0x66778899u;
    ladder.commonItemData.SID = 0xAAu;
    ladder.setObjectSubtypeId(static_cast<unsigned int>(geck::SceneryType::LadderUp));
    ladder.sceneryData.materialId = 7;
    ladder.sceneryData.soundId = 15;
    ladder.sceneryData.ladderData.destTileAndElevation = 0x0002ABCD;

    const geck::Pro got = proRoundTrip(ladder).pro;

    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::SceneryType::LadderUp));
    REQUIRE(got.commonItemData.flagsExt == 0x66778899u);
    REQUIRE(got.sceneryData.materialId == 7);
    REQUIRE(got.sceneryData.soundId == 15);
    REQUIRE(got.sceneryData.ladderData.destTileAndElevation == 0x0002ABCD);
}

TEST_CASE("PRO scenery generic round-trip preserves the trailing field", "[pro][roundtrip][scenery][generic]") {
    geck::Pro generic{ "generic.pro" };
    generic.header.PID = static_cast<int32_t>(
        geck::ProtoId(geck::ObjectType::Scenery, 0x24u).pid());
    generic.commonItemData.flagsExt = 0xBBCCDDEEu;
    generic.commonItemData.SID = 0xFFu;
    generic.setObjectSubtypeId(static_cast<unsigned int>(geck::SceneryType::Generic));
    generic.sceneryData.materialId = 2;
    generic.sceneryData.soundId = 8;
    generic.sceneryData.genericData.unknownField = 0xCAFE1357u; // arbitrary distinctive value

    const geck::Pro got = proRoundTrip(generic).pro;

    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::SceneryType::Generic));
    REQUIRE(got.commonItemData.flagsExt == 0xBBCCDDEEu);
    REQUIRE(got.sceneryData.materialId == 2);
    REQUIRE(got.sceneryData.soundId == 8);
    REQUIRE(got.sceneryData.genericData.unknownField == 0xCAFE1357u);
}
