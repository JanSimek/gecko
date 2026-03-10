#include <catch2/catch_test_macros.hpp>

#include "format/msg/Msg.h"
#include "util/AmmoCaliberNames.h"

#include <map>

TEST_CASE("Extract ammo caliber names from proto.msg ids", "[msg][proto]") {
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

    auto names = geck::ammoCaliberNamesFromProtoMsg(&protoMsg);
    REQUIRE(names.has_value());
    REQUIRE(names->size() == 19);
    REQUIRE((*names)[0] == "None");
    REQUIRE((*names)[8] == "10mm");
    REQUIRE((*names)[9] == ".44 cal");
    REQUIRE((*names)[10] == "14mm");
    REQUIRE((*names)[11] == "12-gauge");
    REQUIRE((*names)[18] == "7.62mm");
}

TEST_CASE("Ammo caliber extraction fails when proto.msg is incomplete", "[msg][proto]") {
    geck::Msg incompleteProtoMsg("proto.msg", {
        { 300, { 300, "", "None" } },
        { 301, { 301, "", "Rocket" } },
    });

    auto names = geck::ammoCaliberNamesFromProtoMsg(&incompleteProtoMsg);
    REQUIRE_FALSE(names.has_value());
}
