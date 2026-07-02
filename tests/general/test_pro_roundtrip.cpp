#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "writer/pro/ProWriter.h"
#include "support/Fixtures.h"
#include "support/ProBuilder.h"
#include "support/TempFile.h"

using namespace geck::test;

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
    REQUIRE(original->type() == geck::Pro::OBJECT_TYPE::ITEM);
    REQUIRE(original->itemType() == geck::Pro::ITEM_TYPE::DRUG);

    TempFile tmpFile{ "test_pro_roundtrip_drug", ".pro" };
    const auto& tempPath = tmpFile.path();

    {
        geck::ProWriter writer{};
        writer.openFile(tempPath);
        REQUIRE(writer.write(*original));
    } // writer destructor closes/flushes the stream before we read it back

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
// type nibble (Pro::type() == (PID & 0x0F000000) >> 24), so a faithful wall
// prototype can be constructed entirely in memory without a fixture.
//
// We set flagsExt and SID to distinctive non-zero sentinels, write, read back,
// and assert they survive. Against the old writeBE32(0) code these reads return
// 0 and the test FAILS; against the fix they round-trip and the test PASSES.
// ---------------------------------------------------------------------------
TEST_CASE("PRO wall round-trip preserves flagsExt and SID", "[pro][roundtrip][wall]") {
    TempFile tmpFile{ "test_pro_roundtrip_wall", ".pro" };
    const auto& tempPath = tmpFile.path();

    // PID type nibble == 3 selects OBJECT_TYPE::WALL. The remaining bits are
    // an arbitrary but distinctive prototype index, preserved verbatim.
    constexpr int32_t WALL_PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::WALL) << 24) | 0x00000005u);

    constexpr uint32_t SENTINEL_FLAGS_EXT = 0xABCD1234u;
    constexpr uint32_t SENTINEL_SID = 0x42u;
    constexpr uint32_t SENTINEL_MATERIAL = 0x07u;

    geck::Pro wall{ tempPath };
    wall.header.PID = WALL_PID;
    wall.header.message_id = 1234;
    wall.header.FID = 0x04000010;
    wall.header.light_distance = 8;
    wall.header.light_intensity = 65536;
    wall.header.flags = 0x00000020; // arbitrary distinctive header flags
    wall.commonItemData.flagsExt = SENTINEL_FLAGS_EXT;
    wall.commonItemData.SID = SENTINEL_SID;
    wall.wallData.materialId = SENTINEL_MATERIAL;

    REQUIRE(wall.type() == geck::Pro::OBJECT_TYPE::WALL);

    {
        geck::ProWriter writer{};
        writer.openFile(tempPath);
        REQUIRE(writer.write(wall));
    } // flush + close before reading back

    geck::ProReader reader{};
    auto reparsed = reader.openFile(tempPath);
    REQUIRE(reparsed != nullptr);
    REQUIRE(reparsed->type() == geck::Pro::OBJECT_TYPE::WALL);

    // Header integrity.
    REQUIRE(reparsed->header.PID == WALL_PID);
    REQUIRE(reparsed->header.message_id == 1234);
    REQUIRE(reparsed->header.FID == 0x04000010);
    REQUIRE(reparsed->header.light_distance == 8);
    REQUIRE(reparsed->header.light_intensity == 65536);
    REQUIRE(reparsed->header.flags == 0x00000020);

    // The actual wall-fix regression assertions: these are the fields the old
    // writer zeroed out.
    REQUIRE(reparsed->commonItemData.flagsExt == SENTINEL_FLAGS_EXT);
    REQUIRE(reparsed->commonItemData.SID == SENTINEL_SID);
    REQUIRE(reparsed->wallData.materialId == SENTINEL_MATERIAL);
}

// ---------------------------------------------------------------------------
// Level 3: per-type read->write->read for the non-item object types, built in
// memory (type comes from the PID nibble). These guard the reader/writer field
// schema against drift - notably that CRITTER and SCENERY write the common
// flagsExt/SID prefix that the reader reads (writeCritterData/writeSceneryData
// previously omitted them, corrupting saved critter/scenery prototypes).
// ---------------------------------------------------------------------------

TEST_CASE("PRO critter round-trip preserves the common header and stats", "[pro][roundtrip][critter]") {
    TempFile tmpFile{ "test_pro_roundtrip_critter", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro critter{ tempPath };
    critter.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::CRITTER) << 24) | 0x42u);
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

    REQUIRE(critter.type() == geck::Pro::OBJECT_TYPE::CRITTER);

    const geck::Pro got = proRoundTrip(critter, tempPath);

    REQUIRE(got.type() == geck::Pro::OBJECT_TYPE::CRITTER);
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
    TempFile tmpFile{ "test_pro_roundtrip_scenery", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro scenery{ tempPath };
    scenery.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::SCENERY) << 24) | 0x10u);
    scenery.header.message_id = 222;
    scenery.header.FID = 0x02000003;
    scenery.header.flags = 0x00000080;
    scenery.commonItemData.flagsExt = 0xCAFEF00Du; // <- bug fields
    scenery.commonItemData.SID = 0x5678u;
    scenery.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::STAIRS));
    scenery.sceneryData.materialId = 4;
    scenery.sceneryData.soundId = 9;
    scenery.sceneryData.stairsData.destTile = 12345;
    scenery.sceneryData.stairsData.destElevation = 2;

    REQUIRE(scenery.type() == geck::Pro::OBJECT_TYPE::SCENERY);

    const geck::Pro got = proRoundTrip(scenery, tempPath);

    REQUIRE(got.type() == geck::Pro::OBJECT_TYPE::SCENERY);
    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::STAIRS));
    REQUIRE(got.commonItemData.flagsExt == 0xCAFEF00Du);
    REQUIRE(got.commonItemData.SID == 0x5678u);
    REQUIRE(got.sceneryData.materialId == 4);
    REQUIRE(got.sceneryData.soundId == 9);
    REQUIRE(got.sceneryData.stairsData.destTile == 12345);
    REQUIRE(got.sceneryData.stairsData.destElevation == 2);
}

TEST_CASE("PRO weapon round-trip preserves the optional weaponFlags field", "[pro][roundtrip][weapon]") {
    TempFile tmpFile{ "test_pro_roundtrip_weapon", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro weapon{ tempPath };
    weapon.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::ITEM) << 24) | 0x20u);
    weapon.header.flags = 0x00000008;
    weapon.commonItemData.flagsExt = 0x11112222u;
    weapon.commonItemData.SID = 0x33u;
    weapon.commonItemData.weight = 5;
    weapon.commonItemData.basePrice = 250;
    weapon.commonItemData.soundId = 7;
    weapon.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::ITEM_TYPE::WEAPON));
    weapon.weaponData.damageMin = 8;
    weapon.weaponData.damageMax = 16;
    weapon.weaponData.ammoPID = 0x00000029;
    weapon.weaponData.ammoCapacity = 30;
    weapon.weaponData.soundId = 4;
    weapon.weaponData.weaponFlags = 0x00000001; // energy-weapon bit; optional trailing field

    const geck::Pro got = proRoundTrip(weapon, tempPath);

    REQUIRE(got.itemType() == geck::Pro::ITEM_TYPE::WEAPON);
    REQUIRE(got.commonItemData.flagsExt == 0x11112222u);
    REQUIRE(got.commonItemData.SID == 0x33u);
    REQUIRE(got.commonItemData.weight == 5);
    REQUIRE(got.weaponData.damageMin == 8);
    REQUIRE(got.weaponData.damageMax == 16);
    REQUIRE(got.weaponData.ammoPID == 0x00000029);
    REQUIRE(got.weaponData.ammoCapacity == 30);
    REQUIRE(got.weaponData.weaponFlags == 0x00000001u);
}

TEST_CASE("PRO container round-trip preserves item subtype data", "[pro][roundtrip][item]") {
    TempFile tmpFile{ "test_pro_roundtrip_container", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro container{ tempPath };
    container.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::ITEM) << 24) | 0x30u);
    container.commonItemData.flagsExt = 0x44445555u;
    container.commonItemData.SID = 0x66u;
    container.commonItemData.materialId = 2;
    container.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::ITEM_TYPE::CONTAINER));
    container.containerData.maxSize = 100;
    container.containerData.flags = 0x0Au;

    const geck::Pro got = proRoundTrip(container, tempPath);

    REQUIRE(got.itemType() == geck::Pro::ITEM_TYPE::CONTAINER);
    REQUIRE(got.commonItemData.flagsExt == 0x44445555u);
    REQUIRE(got.commonItemData.SID == 0x66u);
    REQUIRE(got.commonItemData.materialId == 2);
    REQUIRE(got.containerData.maxSize == 100);
    REQUIRE(got.containerData.flags == 0x0Au);
}

TEST_CASE("PRO tile round-trip omits the common header prefix", "[pro][roundtrip][tile]") {
    TempFile tmpFile{ "test_pro_roundtrip_tile", ".pro" };
    const auto& tempPath = tmpFile.path();

    // TILE (and MISC) are the types WITHOUT the flagsExt/SID prefix.
    geck::Pro tile{ tempPath };
    tile.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::TILE) << 24) | 0x55u);
    tile.header.message_id = 321;
    tile.header.flags = 0x00000002;
    tile.tileData.materialId = 3;

    const geck::Pro got = proRoundTrip(tile, tempPath);

    REQUIRE(got.type() == geck::Pro::OBJECT_TYPE::TILE);
    REQUIRE(got.header.PID == tile.header.PID);
    REQUIRE(got.header.message_id == 321);
    REQUIRE(got.header.flags == 0x00000002);
    REQUIRE(got.tileData.materialId == 3);
}

TEST_CASE("PRO misc round-trip carries no trailing field", "[pro][roundtrip][misc]") {
    TempFile tmpFile{ "test_pro_roundtrip_misc", ".pro" };
    const auto& tempPath = tmpFile.path();

    // MISC carries no type-specific data: the record ends at flagsExt, matching
    // Fallout 2 CE (proto.cc OBJ_TYPE_MISC reads/writes only through extendedFlags).
    geck::Pro misc{ tempPath };
    misc.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::MISC) << 24) | 0x11u);
    misc.header.message_id = 654;
    misc.header.light_distance = 7;
    misc.header.light_intensity = 0x10000;
    misc.header.flags = 0x00000004;
    misc.commonItemData.flagsExt = 0x0000ABCD;

    const geck::Pro got = proRoundTrip(misc, tempPath);

    REQUIRE(got.type() == geck::Pro::OBJECT_TYPE::MISC);
    REQUIRE(got.header.PID == misc.header.PID);
    REQUIRE(got.header.message_id == 654);
    REQUIRE(got.header.light_distance == 7);
    REQUIRE(got.header.light_intensity == 0x10000);
    REQUIRE(got.header.flags == 0x00000004);
    REQUIRE(got.commonItemData.flagsExt == 0x0000ABCD);

    // Exactly 7 BE32 fields (PID, message_id, FID, light_distance, light_intensity,
    // flags, flagsExt) = 28 bytes — not 32 with a phantom trailing field.
    REQUIRE(std::filesystem::file_size(tempPath) == 28);
}

// ---------------------------------------------------------------------------
// Level 4: the remaining item subtypes (ARMOR/AMMO/MISC-item/KEY) and scenery
// subtypes (DOOR/ELEVATOR/LADDER/GENERIC). These tails were the ones NOT
// previously round-trip covered. Per-index array sentinels (e.g. 10+i) make the
// test fail on any dropped/added/reordered field, guarding the hand-enumerated
// reader/writer schema without introducing a serialization visitor.
// ---------------------------------------------------------------------------

TEST_CASE("PRO armor round-trip preserves resist/threshold arrays and common data", "[pro][roundtrip][item][armor]") {
    TempFile tmpFile{ "test_pro_roundtrip_armor", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro armor{ tempPath };
    armor.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::ITEM) << 24) | 0x12u);
    armor.commonItemData.flagsExt = 0xA1A2A3A4u;
    armor.commonItemData.SID = 0xB5u;
    armor.commonItemData.materialId = 1;
    armor.commonItemData.weight = 25;
    armor.commonItemData.basePrice = 1000;
    armor.commonItemData.inventoryFID = 0x00000123;
    armor.commonItemData.soundId = 0x2A;
    armor.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::ITEM_TYPE::ARMOR));
    armor.armorData.armorClass = 20;
    for (int i = 0; i < geck::Pro::DAMAGE_TYPES_ARMOR; ++i) {
        armor.armorData.damageResist[i] = static_cast<uint32_t>(10 + i); // per-index -> catches array drift
        armor.armorData.damageThreshold[i] = static_cast<uint32_t>(30 + i);
    }
    armor.armorData.perk = 7;
    armor.armorData.armorMaleFID = 0x0100002A;
    armor.armorData.armorFemaleFID = 0x0100002B;

    const geck::Pro got = proRoundTrip(armor, tempPath);

    REQUIRE(got.itemType() == geck::Pro::ITEM_TYPE::ARMOR);
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
    TempFile tmpFile{ "test_pro_roundtrip_ammo", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro ammo{ tempPath };
    ammo.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::ITEM) << 24) | 0x13u);
    ammo.commonItemData.flagsExt = 0xC1C2C3C4u;
    ammo.commonItemData.SID = 0xD5u;
    ammo.commonItemData.soundId = 0x1F;
    ammo.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::ITEM_TYPE::AMMO));
    ammo.ammoData.caliber = 4;
    ammo.ammoData.quantity = 50;
    ammo.ammoData.damageModifier = -3; // signed fields must survive
    ammo.ammoData.damageResistModifier = -10;
    ammo.ammoData.damageMultiplier = 2;
    ammo.ammoData.damageTypeModifier = -1;

    const geck::Pro got = proRoundTrip(ammo, tempPath);

    REQUIRE(got.itemType() == geck::Pro::ITEM_TYPE::AMMO);
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
    TempFile tmpFile{ "test_pro_roundtrip_miscitem", ".pro" };
    const auto& tempPath = tmpFile.path();

    // ITEM_TYPE::MISC (a misc *item* with powerType/charges) — distinct from the
    // top-level OBJECT_TYPE::MISC object covered above.
    geck::Pro miscItem{ tempPath };
    miscItem.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::ITEM) << 24) | 0x14u);
    miscItem.commonItemData.flagsExt = 0xE1E2E3E4u;
    miscItem.commonItemData.SID = 0xF5u;
    miscItem.commonItemData.soundId = 0x0C;
    miscItem.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::ITEM_TYPE::MISC));
    miscItem.miscData.powerType = 0x00000029; // ammo PID the item consumes
    miscItem.miscData.charges = 42;

    const geck::Pro got = proRoundTrip(miscItem, tempPath);

    REQUIRE(got.itemType() == geck::Pro::ITEM_TYPE::MISC);
    REQUIRE(got.commonItemData.flagsExt == 0xE1E2E3E4u);
    REQUIRE(got.commonItemData.SID == 0xF5u);
    REQUIRE(got.miscData.powerType == 0x00000029);
    REQUIRE(got.miscData.charges == 42);
}

TEST_CASE("PRO key round-trip preserves keyId", "[pro][roundtrip][item][key]") {
    TempFile tmpFile{ "test_pro_roundtrip_key", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro key{ tempPath };
    key.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::ITEM) << 24) | 0x15u);
    key.commonItemData.flagsExt = 0x10203040u;
    key.commonItemData.SID = 0x50u;
    key.commonItemData.soundId = 0x03;
    key.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::ITEM_TYPE::KEY));
    key.keyData.keyId = 0x0000BEEF;

    const geck::Pro got = proRoundTrip(key, tempPath);

    REQUIRE(got.itemType() == geck::Pro::ITEM_TYPE::KEY);
    REQUIRE(got.commonItemData.flagsExt == 0x10203040u);
    REQUIRE(got.commonItemData.SID == 0x50u);
    REQUIRE(got.keyData.keyId == 0x0000BEEF);
}

TEST_CASE("PRO scenery door round-trip preserves door fields", "[pro][roundtrip][scenery][door]") {
    TempFile tmpFile{ "test_pro_roundtrip_door", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro door{ tempPath };
    door.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::SCENERY) << 24) | 0x21u);
    door.commonItemData.flagsExt = 0x0A0B0C0Du;
    door.commonItemData.SID = 0x0Eu;
    door.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::DOOR));
    door.sceneryData.materialId = 5;
    door.sceneryData.soundId = 11;
    door.sceneryData.doorData.walkThroughFlag = 1;
    door.sceneryData.doorData.unknownField = 0x1234;

    const geck::Pro got = proRoundTrip(door, tempPath);

    REQUIRE(got.type() == geck::Pro::OBJECT_TYPE::SCENERY);
    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::DOOR));
    REQUIRE(got.commonItemData.flagsExt == 0x0A0B0C0Du);
    REQUIRE(got.commonItemData.SID == 0x0Eu);
    REQUIRE(got.sceneryData.materialId == 5);
    REQUIRE(got.sceneryData.soundId == 11);
    REQUIRE(got.sceneryData.doorData.walkThroughFlag == 1);
    REQUIRE(got.sceneryData.doorData.unknownField == 0x1234);
}

TEST_CASE("PRO scenery elevator round-trip preserves elevator fields", "[pro][roundtrip][scenery][elevator]") {
    TempFile tmpFile{ "test_pro_roundtrip_elevator", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro elevator{ tempPath };
    elevator.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::SCENERY) << 24) | 0x22u);
    elevator.commonItemData.flagsExt = 0x11223344u;
    elevator.commonItemData.SID = 0x55u;
    elevator.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::ELEVATOR));
    elevator.sceneryData.materialId = 6;
    elevator.sceneryData.soundId = 13;
    elevator.sceneryData.elevatorData.elevatorType = 3;
    elevator.sceneryData.elevatorData.elevatorLevel = 2;

    const geck::Pro got = proRoundTrip(elevator, tempPath);

    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::ELEVATOR));
    REQUIRE(got.commonItemData.flagsExt == 0x11223344u);
    REQUIRE(got.sceneryData.materialId == 6);
    REQUIRE(got.sceneryData.soundId == 13);
    REQUIRE(got.sceneryData.elevatorData.elevatorType == 3);
    REQUIRE(got.sceneryData.elevatorData.elevatorLevel == 2);
}

TEST_CASE("PRO scenery ladder round-trip preserves the packed dest field", "[pro][roundtrip][scenery][ladder]") {
    TempFile tmpFile{ "test_pro_roundtrip_ladder", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro ladder{ tempPath };
    ladder.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::SCENERY) << 24) | 0x23u);
    ladder.commonItemData.flagsExt = 0x66778899u;
    ladder.commonItemData.SID = 0xAAu;
    ladder.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::LADDER_BOTTOM));
    ladder.sceneryData.materialId = 7;
    ladder.sceneryData.soundId = 15;
    ladder.sceneryData.ladderData.destTileAndElevation = 0x0002ABCD;

    const geck::Pro got = proRoundTrip(ladder, tempPath);

    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::LADDER_BOTTOM));
    REQUIRE(got.commonItemData.flagsExt == 0x66778899u);
    REQUIRE(got.sceneryData.materialId == 7);
    REQUIRE(got.sceneryData.soundId == 15);
    REQUIRE(got.sceneryData.ladderData.destTileAndElevation == 0x0002ABCD);
}

TEST_CASE("PRO scenery generic round-trip preserves the trailing field", "[pro][roundtrip][scenery][generic]") {
    TempFile tmpFile{ "test_pro_roundtrip_generic", ".pro" };
    const auto& tempPath = tmpFile.path();

    geck::Pro generic{ tempPath };
    generic.header.PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::SCENERY) << 24) | 0x24u);
    generic.commonItemData.flagsExt = 0xBBCCDDEEu;
    generic.commonItemData.SID = 0xFFu;
    generic.setObjectSubtypeId(static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::GENERIC));
    generic.sceneryData.materialId = 2;
    generic.sceneryData.soundId = 8;
    generic.sceneryData.genericData.unknownField = 0xCAFE1357u; // arbitrary distinctive value

    const geck::Pro got = proRoundTrip(generic, tempPath);

    REQUIRE(got.objectSubtypeId() == static_cast<unsigned int>(geck::Pro::SCENERY_TYPE::GENERIC));
    REQUIRE(got.commonItemData.flagsExt == 0xBBCCDDEEu);
    REQUIRE(got.sceneryData.materialId == 2);
    REQUIRE(got.sceneryData.soundId == 8);
    REQUIRE(got.sceneryData.genericData.unknownField == 0xCAFE1357u);
}
