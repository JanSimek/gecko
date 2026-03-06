#include <catch2/catch_test_macros.hpp>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"

TEST_CASE("Parse .pro drug file", "[pro]") {
    geck::ProReader pro_reader{};
    auto pro_file = pro_reader.openFile("data/test_item_drug_radx.pro");

    // Verify it's an item type
    REQUIRE(pro_file->type() == geck::Pro::OBJECT_TYPE::ITEM);
    REQUIRE(pro_file->itemType() == geck::Pro::ITEM_TYPE::DRUG);

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

    REQUIRE(geck::Pro::hasFlag(pro_file->header.flags, geck::Pro::ObjectFlags::OBJECT_FLAT));

    // TODO: check other flags

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