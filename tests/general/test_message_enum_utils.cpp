#include <catch2/catch_test_macros.hpp>

#include "format/msg/Msg.h"
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
