#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <sstream>
#include <string>

#include "cli/ProtoExport.h"
#include "resource/GameResources.h"
#include "support/ByteWriter.h"
#include "support/CliJson.h"
#include "support/DataTree.h"
#include "support/EngineProtoBytes.h"

using json = nlohmann::json;
using geck::test::ByteWriter;
using geck::test::critterProto;
using geck::test::itemProtoHeader;
using geck::test::weaponProto;
using namespace geck;

namespace {

// A mounted data directory holding four readable item protos, one .lst entry whose file is missing,
// and one critter, plus the .msg files that name them.
class ProtoFixture {
public:
    ProtoFixture() {
        _tree.write("proto/items/items.lst", "weapon.pro\nmissing.pro\nammo.pro\narmor.pro\nmisc.pro\n");
        _tree.write("proto/critters/critters.lst", "critter.pro\n");
        _tree.write("scripts/scripts.lst", "a.int\nb.int\nc.int\nd.int\ne.int\ntest.int\n");
        _tree.write("text/english/game/pro_item.msg",
            "{100}{}{Test Rifle}\n{101}{}{A rifle.}\n{300}{}{Test Ammo}\n{400}{}{Test Armor}\n{500}{}{Test Cell}\n");
        _tree.write("text/english/game/pro_crit.msg", "{200}{}{Test Critter}\n");
        _tree.write("text/english/game/proto.msg",
            "{101}{}{Metal}\n{251}{}{laser}\n{252}{}{fire}\n{308}{}{10mm}\n{401}{}{Quadruped}\n");
        _tree.write("text/english/game/perk.msg", "{159}{}{Long Range}\n");

        // Primary attack mode 6, secondary 7, Big Gun, Two Handed and sfall's Energy bit.
        const ByteWriter weapon = weaponProto(1, 100, 0x00000776);
        REQUIRE(weapon.size() == 122);
        _tree.writeBytes("proto/items/weapon.pro", weapon.data());

        ByteWriter ammo = itemProtoHeader(3, 300, 0, 4);
        ammo.be32s({ 8, 24, -10, 25, 2, 1 });
        REQUIRE(ammo.size() == 81);
        _tree.writeBytes("proto/items/ammo.pro", ammo.data());

        ByteWriter armor = itemProtoHeader(4, 400, 0, 0);
        armor.be32s({ 20, 30, 20, 20, 10, 30, 500, 20, 4, 0, 0, 0, 0, 0, 0, -1, 0x0100000C, 0x01000005 });
        REQUIRE(armor.size() == 129);
        _tree.writeBytes("proto/items/armor.pro", armor.data());

        ByteWriter misc = itemProtoHeader(5, 500, 0, 5);
        misc.be32s({ 38, 3, 200 });
        REQUIRE(misc.size() == 69);
        _tree.writeBytes("proto/items/misc.pro", misc.data());

        // Base stat i holds 100 + i and bonus stat i holds 200 + i, so each value names the stat slot
        // it was read from; skill i holds i.
        const ByteWriter critter = critterProto(100, 200, 0);
        REQUIRE(critter.size() == 416);
        _tree.writeBytes("proto/critters/critter.pro", critter.data());
    }

    json run(const cli::ProtoExportOptions& options = {}) {
        return test::runJson([&](std::ostream& out) { return cli::exportProtos(_tree.mount(), options, out); });
    }

private:
    test::DataTree _tree{ "geck_proto_export_test" };
};

const json& protoByPid(const json& out, std::uint32_t pid) {
    for (const auto& proto : out["protos"]) {
        if (proto["pid"] == pid) {
            return proto;
        }
    }
    FAIL("pid " << pid << " not exported");
    static const json none;
    return none;
}

} // namespace

TEST_CASE("exportProtos lists every .lst entry and reports the ones that do not load", "[proto][export]") {
    ProtoFixture fixture;
    const json out = fixture.run();

    REQUIRE(out["lists"].size() == 2);
    CHECK(out["lists"][0]["path"] == "proto/items/items.lst");
    CHECK(out["lists"][0]["entries"] == 5);
    CHECK(out["lists"][1]["entries"] == 1);
    CHECK(out["protoCount"] == 5);
    REQUIRE(out["unreadable"].size() == 1);
    CHECK(out["unreadable"][0]["pid"] == 2);
    CHECK(out["unreadable"][0]["file"] == "missing.pro");
}

TEST_CASE("exportProtos decodes a weapon the way the engine reads it", "[proto][export]") {
    ProtoFixture fixture;
    const json out = fixture.run();
    const json& weapon = protoByPid(out, 1);

    CHECK(weapon["kind"] == "item");
    CHECK(weapon["itemType"] == "weapon");
    CHECK(weapon["name"] == "Test Rifle");
    CHECK(weapon["description"] == "A rifle.");
    CHECK(weapon["weight"] == 7);
    CHECK(weapon["cost"] == 250);
    CHECK(weapon["inventoryFid"] == 0x07000020);
    CHECK(weapon["material"] == json({ { "id", 1 }, { "name", "Metal" } }));
    CHECK(weapon["sid"] == -1);
    CHECK(weapon["script"].is_null());

    // The attack modes come from the extended flags' low byte (item.cc), not from header.flags.
    CHECK(weapon["extendedFlags"]["raw"] == 0x776);
    CHECK(weapon["extendedFlags"]["bigGun"] == true);
    CHECK(weapon["extendedFlags"]["twoHanded"] == true);
    CHECK(weapon["extendedFlags"]["energyWeapon"] == true);
    CHECK(weapon["extendedFlags"]["hiddenItem"] == false);
    CHECK(weapon["attackModes"]["primary"] == json({ { "index", 6 }, { "type", "ranged" }, { "animation", "fireSingle" } }));
    CHECK(weapon["attackModes"]["secondary"] == json({ { "index", 7 }, { "type", "ranged" }, { "animation", "fireBurst" } }));

    const json& w = weapon["weapon"];
    CHECK(w["animationCode"] == 6);
    CHECK(w["damage"] == json({ { "min", 10 }, { "max", 20 } }));
    CHECK(w["damageType"] == json({ { "id", 1 }, { "name", "laser" } }));
    CHECK(w["range"] == json({ { "primary", 25 }, { "secondary", 20 } }));
    CHECK(w["projectilePid"] == -1);
    CHECK(w["minStrength"] == 4);
    CHECK(w["apCost"] == json({ { "primary", 5 }, { "secondary", 6 } }));
    CHECK(w["criticalFail"] == 2);
    CHECK(w["perk"] == json({ { "id", 58 }, { "name", "Long Range" } }));
    CHECK(w["burstRounds"] == 10);
    CHECK(w["caliber"] == json({ { "id", 8 }, { "name", "10mm" } }));
    CHECK(w["ammoPid"] == 3);
    CHECK(w["ammoCapacity"] == 30);
    CHECK(w["soundId"] == 65); // 'A'
}

TEST_CASE("exportProtos names ammo, armour and misc fields for what the engine does with them", "[proto][export]") {
    ProtoFixture fixture;
    const json out = fixture.run();

    // Pro::AmmoData calls these damageModifier/damageResistModifier/damageTypeModifier; the engine
    // reads AC adjust, DR adjust, damage multiplier and divisor.
    CHECK(protoByPid(out, 3)["ammo"]
        == json({ { "caliber", { { "id", 8 }, { "name", "10mm" } } }, { "quantity", 24 }, { "acModifier", -10 },
            { "drModifier", 25 }, { "damageMultiplier", 2 }, { "damageDivisor", 1 } }));

    const json& armor = protoByPid(out, 4)["armor"];
    CHECK(armor["ac"] == 20);
    CHECK(armor["dr"] == json({ { "normal", 30 }, { "laser", 20 }, { "fire", 20 }, { "plasma", 10 }, { "electrical", 30 }, { "emp", 500 }, { "explosion", 20 } }));
    CHECK(armor["dt"]["normal"] == 4);
    CHECK(armor["dt"]["explosion"] == 0);
    CHECK(armor["perk"].is_null());
    CHECK(armor["maleFid"] == 0x0100000C);
    CHECK(armor["femaleFid"] == 0x01000005);

    // A misc item stores three words; reading two shifted each one into the wrong field.
    CHECK(protoByPid(out, 5)["misc"] == json({ { "powerTypePid", 38 }, { "powerType", 3 }, { "charges", 200 } }));
}

TEST_CASE("exportProtos reads a critter's stat blocks by stat id", "[proto][export]") {
    ProtoFixture fixture;
    const json out = fixture.run();
    const json& critter = protoByPid(out, 0x01000001);

    CHECK(critter["kind"] == "critter");
    CHECK(critter["name"] == "Test Critter");
    CHECK(critter["description"].is_null()); // pro_crit.msg has no {201}: reported, not invented
    CHECK(critter["headFid"] == -1);
    CHECK(critter["aiPacket"] == 77);
    CHECK(critter["team"] == 1);
    CHECK(critter["critterFlags"]["raw"] == 0x0802);
    CHECK(critter["critterFlags"]["barter"] == true);
    CHECK(critter["critterFlags"]["noFlatten"] == true);
    CHECK(critter["critterFlags"]["invulnerable"] == false);
    CHECK(critter["script"] == json({ { "programIndex", 5 }, { "name", "test.int" } }));

    const json& base = critter["base"];
    CHECK(base["special"]["strength"] == 100);
    CHECK(base["special"]["luck"] == 106);
    CHECK(base["maxHitPoints"] == 107);
    CHECK(base["unarmedDamage"] == 110);
    CHECK(base["betterCriticals"] == 116);
    CHECK(base["dt"]["normal"] == 117);
    CHECK(base["dr"]["normal"] == 124);
    CHECK(base["dr"]["explosion"] == 130);
    CHECK(base["radiationResistance"] == 131);
    CHECK(base["gender"] == 134);

    // The bonus block splits the same way as the base block: 7 DT, 7 DR, radiation, poison.
    const json& bonus = critter["bonus"];
    CHECK(bonus["dt"]["explosion"] == 223);
    CHECK(bonus["dr"]["normal"] == 224);
    CHECK(bonus["dr"]["explosion"] == 230);
    CHECK(bonus["radiationResistance"] == 231);
    CHECK(bonus["poisonResistance"] == 232);
    CHECK(bonus["gender"] == 234);

    CHECK(critter["skills"]["smallGuns"] == 0);
    CHECK(critter["skills"]["outdoorsman"] == 17);
    CHECK(critter["bodyType"] == json({ { "id", 1 }, { "name", "Quadruped" } }));
    CHECK(critter["xp"] == 150);
    CHECK(critter["killType"] == 3);
    CHECK(critter["damageType"] == json({ { "id", 2 }, { "name", "fire" } }));
}

TEST_CASE("exportProtos filters by kind and item type", "[proto][export]") {
    ProtoFixture fixture;

    cli::ProtoExportOptions ammoOnly;
    REQUIRE_FALSE(cli::parseProtoFilter("", "ammo", ammoOnly).has_value());
    const json ammo = fixture.run(ammoOnly);
    REQUIRE(ammo["lists"].size() == 1); // an item type implies items only
    REQUIRE(ammo["protos"].size() == 1);
    CHECK(ammo["protos"][0]["pid"] == 3);

    cli::ProtoExportOptions crittersOnly;
    REQUIRE_FALSE(cli::parseProtoFilter("critter", "", crittersOnly).has_value());
    const json critters = fixture.run(crittersOnly);
    REQUIRE(critters["protos"].size() == 1);
    CHECK(critters["protos"][0]["kind"] == "critter");
}

TEST_CASE("parseProtoFilter rejects what it cannot honour", "[proto][export]") {
    cli::ProtoExportOptions options;
    CHECK(cli::parseProtoFilter("robot", "", options).has_value());
    CHECK(cli::parseProtoFilter("", "gun", options).has_value());
    CHECK(cli::parseProtoFilter("critter", "weapon", options).has_value());

    REQUIRE_FALSE(cli::parseProtoFilter("item", "key", options).has_value());
    CHECK(options.kind == ObjectType::Item);
    CHECK(options.itemType == ItemType::Key);
}

TEST_CASE("exportProtos without mounted data is an error", "[proto][export]") {
    resource::GameResources resources;
    std::ostringstream oss;
    CHECK(cli::exportProtos(resources, {}, oss) == 1);
    CHECK(oss.str().find("proto/items/items.lst") != std::string::npos);
}
