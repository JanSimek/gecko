#include <catch2/catch_test_macros.hpp>

#include "format/msg/Msg.h"
#include "util/MessageEnumUtils.h"

TEST_CASE("loadMessageRange reads consecutive messages as enum labels", "[message_enum_utils]") {
    geck::Msg msgFile("proto.msg", {
                                       { 100, { 100, "", "Glass" } },
                                       { 101, { 101, "", "Metal" } },
                                       { 102, { 102, "", "Plastic" } },
                                   });

    auto names = geck::loadMessageRange(&msgFile, 100, 3);
    REQUIRE(names.has_value());
    REQUIRE(names->size() == 3);
    CHECK((*names)[0] == "Glass");
    CHECK((*names)[2] == "Plastic");
}

TEST_CASE("loadMessageOptions pairs each engine value with its message label", "[message_enum_utils]") {
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
    CHECK((*options)[0].value == 58);
    CHECK((*options)[0].label == "Weapon Long Range");
    CHECK((*options)[2].value == 62);
    CHECK((*options)[2].label == "Powered Armor");
}

TEST_CASE("loadMessageRange and loadMessageOptions fail when a message is missing", "[message_enum_utils]") {
    geck::Msg incompleteMsg("proto.msg", {
                                             { 100, { 100, "", "Glass" } },
                                         });

    CHECK_FALSE(geck::loadMessageRange(&incompleteMsg, 100, 2).has_value());

    const geck::MessageEnumSpec specs[] = {
        { 58, 159 },
    };
    CHECK_FALSE(geck::loadMessageOptions(&incompleteMsg, specs).has_value());
}
