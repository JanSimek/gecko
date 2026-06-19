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
