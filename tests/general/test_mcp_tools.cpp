#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "format/pro/ProtoTypes.h"
#include "mcp/McpServer.h"
#include "resource/GameResources.h"

#include "support/DataTree.h"
#include "support/EngineProtoBytes.h"
#include "support/Fixtures.h"
#include "support/MapFileIo.h"
#include "support/McpCall.h"
#include "support/ShopMap.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;

// mounts exists because a data path that did not open used to be skipped in silence, and every
// answer afterwards came from whatever else mounted while reading as confidently as a correct one.
TEST_CASE("mounts reports what is mounted and what refused to", "[mcp]") {
    SECTION("a healthy server reports its mounts and no failures") {
        test::DataTree tree{ "gecko_mounts_ok" };
        tree.write("text/english/dialog/patchinf.msg",
            "{100}{}{You are running RPU v2.4.34. based on killap's F2 Restoration Project.}\n");

        auto& resources = tree.mount();
        mcp::McpServer server(resources);

        const json out = mcpToolJson(server, "mounts");
        CHECK(out.contains("mounts"));
        CHECK(out.contains("lookupOrder"));
        CHECK_FALSE(out.contains("failedMounts"));
        // Read from the data rather than from a checkout, so it works for a .dat too.
        CHECK(out["rpuVersion"] == "v2.4.34");
    }

    SECTION("a source tree reports the placeholder version, which is the answer") {
        test::DataTree tree{ "gecko_mounts_src" };
        tree.write("text/english/dialog/patchinf.msg",
            "{100}{}{You are running RPU v2.x.x, based on killap's F2 Restoration Project.}\n");

        auto& resources = tree.mount();
        mcp::McpServer server(resources);

        const json out = mcpToolJson(server, "mounts");
        CHECK(out["rpuVersion"] == "v2.x.x");
        // Unpackaged source is a development line and not any release; say so rather than leave it
        // to be mistaken for one.
        CHECK(out.contains("rpuVersionNote"));
    }

    SECTION("a .dat mount is reported as one, with its size") {
        // gitDescribe used to answer this and could say nothing about a .dat, which is the mount
        // whose version actually matters. Reading the data covers both kinds.
        resource::GameResources resources;
        resources.files().addDataPath(test::f2ResDat());
        mcp::McpServer server(resources);

        const json out = mcpToolJson(server, "mounts");
        REQUIRE(out["mounts"].size() == 1);
        CHECK(out["mounts"][0]["kind"] == "dat");
        CHECK(out["mounts"][0]["bytes"] > 0);
        CHECK_FALSE(out.contains("failedMounts"));
    }

    SECTION("a data path that does not exist is reported, not skipped") {
        resource::GameResources resources;
        resources.files().addDataPath("/gecko-test/definitely/not/here.dat");
        mcp::McpServer server(resources);

        const json out = mcpToolJson(server, "mounts");
        REQUIRE(out.contains("failedMounts"));
        REQUIRE(out["failedMounts"].size() == 1);
        CHECK(out["failedMounts"][0]["reason"] == "does not exist");
    }
}

// proto_info reports effective figures, base plus bonus. Base alone reads a Turret at 30 hit points
// against the 75 the engine gives it, and every critter in the game as resisting nothing, because a
// critter's resistances live entirely in the bonus arrays.
TEST_CASE("proto_info reports a critter's effective stats and skills", "[mcp]") {
    test::DataTree tree{ "gecko_proto_info" };
    tree.write("proto/critters/critters.lst", "critter.pro\n");
    tree.write("text/english/game/pro_crit.msg", "{200}{}{Test Critter}\n");
    // Base stat i holds 100 + i and bonus stat i 1000 + i, so a reported figure names both slots it
    // came from; skill i holds 50 + i.
    tree.writeBytes("proto/critters/critter.pro", critterProto(100, 1000, 50).data());

    auto& resources = tree.mount();
    mcp::McpServer server(resources);

    const json out = mcpToolJson(server, "proto_info", { { "pid", ProtoId(ObjectType::Critter, 1).pid() } });
    const json entry = out.is_array() ? out[0] : out;
    REQUIRE(entry.contains("effective"));
    const json& effective = entry["effective"];

    SECTION("stats are base plus bonus, not base") {
        // Base block: special[0..6], maxHitPoints@7, actionPoints@8, armorClass@9.
        CHECK(effective["maxHitPoints"] == 107 + 1007);
        CHECK(effective["armorClass"] == 109 + 1009);
        CHECK(entry["base"]["maxHitPoints"] == 107);
        CHECK(entry["bonus"]["maxHitPoints"] == 1007);
    }

    SECTION("every skill is reported, in Skill enum order, keyed as export_protos keys them") {
        REQUIRE(entry.contains("skills"));
        // Indexed positionally: skill_defs.h opens with SKILL_INVALID = -1, so the enum's values sit
        // one below their slot and using them would shift every skill by one.
        CHECK(entry["skills"]["smallGuns"] == 50);
        CHECK(entry["skills"]["outdoorsman"] == 50 + 17);
        CHECK(entry["skills"].size() == 18);
    }

    SECTION("the DR bonuses straddle the two arrays") {
        // The base block runs DT[7] at slots 17..23 then DR[9] at 24..32, but the bonus block packs
        // its 16 resistance words 8 + 8: bonusDT[8] at 17..24, bonusDR[8] at 25..32. So the
        // normal-damage DR bonus is the LAST word of bonusDamageThreshold -- slot 24, value 1024 --
        // and not the first word of bonusDamageResistance, slot 25.
        REQUIRE(effective.contains("dr"));
        CHECK(effective["dt"]["normal"] == 117 + 1017);
        CHECK(effective["dr"]["normal"] == 124 + 1024);
        // Reading the two arrays index-for-index is the bug this guards: it would shift every
        // resistance one slot and give 1025 here, which looks just as plausible.
        CHECK(effective["dr"]["normal"] != 124 + 1025);
    }
}

// The weapon block is what turned "which weapons carry Long Range?" from opening .pro files by hand
// into one call — and getting the perk wrong is quiet, because a wrong perk name reads perfectly.
TEST_CASE("proto_info reports a weapon's perk and numbers", "[mcp]") {
    test::DataTree tree{ "gecko_proto_weapon" };
    tree.write("proto/items/items.lst", "weapon.pro\n");
    tree.write("text/english/game/pro_item.msg", "{100}{}{Test Rifle}\n{101}{}{A rifle.}\n");
    tree.write("text/english/game/proto.msg", "{101}{}{Metal}\n{251}{}{laser}\n{308}{}{10mm}\n");
    tree.write("text/english/game/perk.msg", "{159}{}{Long Range}\n");

    const test::ByteWriter weapon = weaponProto(1, 100, 0x00000376);
    REQUIRE(weapon.size() == 122);
    tree.writeBytes("proto/items/weapon.pro", weapon.data());

    auto& resources = tree.mount();
    mcp::McpServer server(resources);

    const json out = mcpToolJson(server, "proto_info", { { "pid", ProtoId(ObjectType::Item, 1).pid() } });
    const json entry = out.is_array() ? out[0] : out;
    REQUIRE(entry.contains("weapon"));
    const json& w = entry["weapon"];

    // perk_defs.h opens with PERK_INVALID = -1, so every perk's value sits one below its line
    // position. Numbering from the first line instead put shotguns under Long Range once, and the
    // result looked entirely plausible. The name is perk.msg's, at 101 + perk.
    CHECK(w["perk"]["id"] == 58);
    CHECK(w["perk"]["name"] == "Long Range");
    CHECK(w["damage"]["min"] == 10);
    CHECK(w["damage"]["max"] == 20);
    CHECK(w["damageType"]["name"] == "laser");
    CHECK(w["range"]["primary"] == 25);
    CHECK(w["range"]["secondary"] == 20);
    CHECK(w["apCost"]["primary"] == 5);
    CHECK(w["apCost"]["secondary"] == 6);
    CHECK(w["minStrength"] == 4);
    CHECK(w["burstRounds"] == 10);
    CHECK(w["ammoCapacity"] == 30);
    CHECK(entry["name"] == "Test Rifle");
    CHECK(entry["type"] == "Item");
    CHECK(entry["file"] == "weapon.pro");
}

TEST_CASE("McpServer answers data-backed tool calls on a map read off disk", "[mcp]") {
    const TempFile map{ "geck_mcp_tools_map", ".map" };
    writeMapFile(ShopMap::build(), map.path());
    resource::GameResources resources;
    mcp::McpServer server(resources);

    const json grid = mcpToolJson(server, "dump_grid", { { "map", map.path().string() }, { "elevation", 0 } });
    CHECK(grid.at("elevations").at(0).at("objects").size() == ShopMap::OBJECT_COUNT);

    const json reach = mcpCall(server, "reachability", { { "map", map.path().string() } });
    CHECK(reach["result"]["isError"] == false);
}
