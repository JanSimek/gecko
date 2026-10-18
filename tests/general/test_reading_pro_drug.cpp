#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "support/Fixtures.h"

TEST_CASE("Parse .pro drug file", "[pro]") {
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