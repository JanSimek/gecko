#include <catch2/catch_test_macros.hpp>

#include "format/msg/Msg.h"
#include "util/FalloutEngineEnums.h"
#include "util/MessageEnumUtils.h"

TEST_CASE("Load message ranges for dynamic enum labels", "[msg][enum]") {
    geck::Msg msgFile("proto.msg", {
                                       { 100, { 100, "", "Glass" } },
                                       { 101, { 101, "", "Metal" } },
                                       { 102, { 102, "", "Plastic" } },
                                   });

    auto names = geck::loadMessageRange(&msgFile, 100, 3);
    REQUIRE(names.has_value());
    REQUIRE(names->size() == 3);
    REQUIRE((*names)[0] == "Glass");
    REQUIRE((*names)[2] == "Plastic");
}

TEST_CASE("Load message options for value-backed enum labels", "[msg][enum]") {
    geck::Msg perkMsg("perk.msg", {
                                      { 159, { 159, "", "Weapon Long Range" } },
                                      { 160, { 160, "", "Weapon Accurate" } },
                                      { 163, { 163, "", "Powered Armor" } },
                                  });

    const geck::MessageEnumSpec specs[] = {
        { 58, 159 },
        { 59, 160 },
        { 62, 163 },
    };

    auto options = geck::loadMessageOptions(&perkMsg, specs);
    REQUIRE(options.has_value());
    REQUIRE(options->size() == 3);
    REQUIRE((*options)[0].value == 58);
    REQUIRE((*options)[0].label == "Weapon Long Range");
    REQUIRE((*options)[2].value == 62);
    REQUIRE((*options)[2].label == "Powered Armor");
}

TEST_CASE("Missing enum messages fail extraction", "[msg][enum]") {
    geck::Msg incompleteMsg("proto.msg", {
                                             { 100, { 100, "", "Glass" } },
                                         });

    REQUIRE_FALSE(geck::loadMessageRange(&incompleteMsg, 100, 2).has_value());

    const geck::MessageEnumSpec specs[] = {
        { 58, 159 },
    };
    REQUIRE_FALSE(geck::loadMessageOptions(&incompleteMsg, specs).has_value());
}

TEST_CASE("The ammo caliber labels are proto.msg 300..318", "[msg][proto]") {
    geck::Msg protoMsg("proto.msg", {
                                        { 300, { 300, "", "None" } },
                                        { 301, { 301, "", "Rocket" } },
                                        { 302, { 302, "", "Flamethrower Fuel" } },
                                        { 303, { 303, "", "C Energy Cell" } },
                                        { 304, { 304, "", "D Energy Cell" } },
                                        { 305, { 305, "", ".223" } },
                                        { 306, { 306, "", "5mm" } },
                                        { 307, { 307, "", ".40 cal" } },
                                        { 308, { 308, "", "10mm" } },
                                        { 309, { 309, "", ".44 cal" } },
                                        { 310, { 310, "", "14mm" } },
                                        { 311, { 311, "", "12-gauge" } },
                                        { 312, { 312, "", "9mm" } },
                                        { 313, { 313, "", "BB" } },
                                        { 314, { 314, "", ".45 cal" } },
                                        { 315, { 315, "", "2mm" } },
                                        { 316, { 316, "", "4.7mm caseless" } },
                                        { 317, { 317, "", "HN needler" } },
                                        { 318, { 318, "", "7.62mm" } },
                                    });

    using namespace geck::fallout;
    auto names = geck::loadMessageRange(&protoMsg, protoMessageId(CaliberType::None), enumCount<CaliberType>());
    REQUIRE(names.has_value());
    REQUIRE(names->size() == 19);
    REQUIRE((*names)[0] == "None");
    REQUIRE((*names)[8] == "10mm");
    REQUIRE((*names)[11] == "12-gauge");
    REQUIRE((*names)[18] == "7.62mm");
}
