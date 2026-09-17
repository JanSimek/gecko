#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <system_error>

#include "cli/ProtoExport.h"
#include "resource/GameResources.h"
#include "support/ByteWriter.h"

using json = nlohmann::json;
using geck::test::ByteWriter;
namespace fs = std::filesystem;
using namespace geck;

namespace {

void writeText(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

void writeBytes(const fs::path& path, const ByteWriter& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data().data()), static_cast<std::streamsize>(bytes.size()));
}

ByteWriter& be32s(ByteWriter& w, std::initializer_list<std::int32_t> values) {
    for (const auto value : values) {
        w.be32(static_cast<std::uint32_t>(value));
    }
    return w;
}

// The item header exactly as fallout2-ce protoRead reads it: pid, messageId, fid, lightDistance,
// lightIntensity, flags, extendedFlags, sid, type, material, size, weight, cost, inventoryFid, then a
// one-byte soundId. Built by hand rather than with ProWriter, so the test pins the engine's layout
// instead of gecko agreeing with itself.
ByteWriter itemHeader(std::int32_t pid, std::int32_t messageId, std::uint32_t extendedFlags, std::int32_t type) {
    ByteWriter w;
    be32s(w, { pid, messageId, 0x07000010, 0, 0, 0x00000008 });
    w.be32(extendedFlags);
    be32s(w, { -1, type, 1 /* Metal */, 3, 7, 250, 0x07000020 });
    w.u8('0');
    return w;
}

// A mounted data directory holding four readable item protos, one .lst entry whose file is missing,
// and one critter, plus the .msg files that name them.
class ProtoFixture {
public:
    ProtoFixture() {
        std::error_code ec;
        fs::remove_all(_root, ec);

        writeText(_root / "proto/items/items.lst", "weapon.pro\nmissing.pro\nammo.pro\narmor.pro\nmisc.pro\n");
        writeText(_root / "proto/critters/critters.lst", "critter.pro\n");
        writeText(_root / "scripts/scripts.lst", "a.int\nb.int\nc.int\nd.int\ne.int\ntest.int\n");
        writeText(_root / "text/english/game/pro_item.msg",
            "{100}{}{Test Rifle}\n{101}{}{A rifle.}\n{300}{}{Test Ammo}\n{400}{}{Test Armor}\n{500}{}{Test Cell}\n");
        writeText(_root / "text/english/game/pro_crit.msg", "{200}{}{Test Critter}\n");
        writeText(_root / "text/english/game/proto.msg",
            "{101}{}{Metal}\n{251}{}{laser}\n{252}{}{fire}\n{308}{}{10mm}\n{401}{}{Quadruped}\n");
        writeText(_root / "text/english/game/perk.msg", "{159}{}{Long Range}\n");

        // Primary attack mode 6, secondary 7, Big Gun and Two Handed.
        ByteWriter weapon = itemHeader(1, 100, 0x00000376, 3);
        be32s(weapon, { 6, 10, 20, 1 /* laser */, 25, 20, -1, 4, 5, 6, 2, 58 /* Weapon Long Range */, 10, 8 /* 10mm */, 3, 30 });
        weapon.u8('A');
        REQUIRE(weapon.size() == 122);
        writeBytes(_root / "proto/items/weapon.pro", weapon);

        ByteWriter ammo = itemHeader(3, 300, 0, 4);
        be32s(ammo, { 8, 24, -10, 25, 2, 1 });
        REQUIRE(ammo.size() == 81);
        writeBytes(_root / "proto/items/ammo.pro", ammo);

        ByteWriter armor = itemHeader(4, 400, 0, 0);
        be32s(armor, { 20, 30, 20, 20, 10, 30, 500, 20, 4, 0, 0, 0, 0, 0, 0, -1, 0x0100000C, 0x01000005 });
        REQUIRE(armor.size() == 129);
        writeBytes(_root / "proto/items/armor.pro", armor);

        ByteWriter misc = itemHeader(5, 500, 0, 5);
        be32s(misc, { 38, 3, 200 });
        REQUIRE(misc.size() == 69);
        writeBytes(_root / "proto/items/misc.pro", misc);

        // The critter layout of fallout2-ce protoRead + protoCritterDataRead. Base stat i holds 100 + i
        // and bonus stat i holds 200 + i, so each value names the stat slot it was read from.
        ByteWriter critter;
        be32s(critter, { 0x01000001, 200, 0x01000040, 0, 0, 0x20000000, 0x6000, 0x04000005, -1, 77, 1 });
        critter.be32(0x0802); // Barter + CRITTER_FLAT
        for (std::int32_t stat = 0; stat < 35; ++stat) {
            critter.be32(static_cast<std::uint32_t>(100 + stat));
        }
        for (std::int32_t stat = 0; stat < 35; ++stat) {
            critter.be32(static_cast<std::uint32_t>(200 + stat));
        }
        for (std::int32_t skill = 0; skill < 18; ++skill) {
            critter.be32(static_cast<std::uint32_t>(skill));
        }
        be32s(critter, { 1 /* quadruped */, 150, 3, 2 /* fire */ });
        REQUIRE(critter.size() == 416);
        writeBytes(_root / "proto/critters/critter.pro", critter);

        _resources.files().addDataPath(_root.string());
    }

    ~ProtoFixture() {
        std::error_code ec;
        fs::remove_all(_root, ec);
    }

    ProtoFixture(const ProtoFixture&) = delete;
    ProtoFixture& operator=(const ProtoFixture&) = delete;

    json run(const cli::ProtoExportOptions& options = {}) {
        std::ostringstream oss;
        REQUIRE(cli::exportProtos(_resources, options, oss) == 0);
        return json::parse(oss.str());
    }

private:
    fs::path _root = fs::path(GECK_TEST_TMP_DIR) / "geck_proto_export_test";
    resource::GameResources _resources;
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
    CHECK(weapon["extendedFlags"]["raw"] == 0x376);
    CHECK(weapon["extendedFlags"]["bigGun"] == true);
    CHECK(weapon["extendedFlags"]["twoHanded"] == true);
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
    CHECK(w["soundId"] == 65);    // 'A'
    CHECK(w["weaponFlags"] == 0); // a 122-byte engine record has no trailing word
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
    CHECK(options.kind == Pro::OBJECT_TYPE::ITEM);
    CHECK(options.itemType == Pro::ITEM_TYPE::KEY);
}

TEST_CASE("exportProtos without mounted data is an error", "[proto][export]") {
    resource::GameResources resources;
    std::ostringstream oss;
    CHECK(cli::exportProtos(resources, {}, oss) == 1);
    CHECK(oss.str().find("proto/items/items.lst") != std::string::npos);
}
