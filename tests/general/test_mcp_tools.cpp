#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "cli/MapAnalyzer.h"
#include "mcp/McpServer.h"
#include "resource/GameResources.h"

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "writer/map/MapWriter.h"
#include "support/ProStubProvider.h"
#include "support/TempFile.h"
#include "support/ByteWriter.h"
#include "support/Fixtures.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;
namespace fs = std::filesystem;

namespace {

// The JSON a tool returned, unwrapped from the MCP content envelope.
json callTool(mcp::McpServer& server, const std::string& name, const json& arguments) {
    const json resp = server.handleMessage({ { "jsonrpc", "2.0" }, { "id", 9 }, { "method", "tools/call" },
        { "params", { { "name", name }, { "arguments", arguments } } } });
    REQUIRE(resp.contains("result"));
    const std::string text = resp["result"]["content"][0]["text"].get<std::string>();
    return json::parse(text);
}

void writeBytes(const fs::path& path, const test::ByteWriter& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data().data()), static_cast<std::streamsize>(bytes.size()));
}

void writeText(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

} // namespace

// mounts exists because a data path that did not open used to be skipped in silence, and every
// answer afterwards came from whatever else mounted while reading as confidently as a correct one.
TEST_CASE("mounts reports what is mounted and what refused to", "[mcp][mounts]") {
    SECTION("a healthy server reports its mounts and no failures") {
        const fs::path root = fs::temp_directory_path() / "gecko_mounts_ok";
        fs::remove_all(root);
        writeText(root / "text/english/dialog/patchinf.msg",
            "{100}{}{You are running RPU v2.4.34. based on killap's F2 Restoration Project.}\n");

        resource::GameResources resources;
        resources.files().addDataPath(root.string());
        mcp::McpServer server(resources);

        const json out = callTool(server, "mounts", json::object());
        CHECK(out.contains("mounts"));
        CHECK(out.contains("lookupOrder"));
        CHECK_FALSE(out.contains("failedMounts"));
        // Read from the data rather than from a checkout, so it works for a .dat too.
        CHECK(out["rpuVersion"] == "v2.4.34");

        fs::remove_all(root);
    }

    SECTION("a source tree reports the placeholder version, which is the answer") {
        const fs::path root = fs::temp_directory_path() / "gecko_mounts_src";
        fs::remove_all(root);
        writeText(root / "text/english/dialog/patchinf.msg",
            "{100}{}{You are running RPU v2.x.x, based on killap's F2 Restoration Project.}\n");

        resource::GameResources resources;
        resources.files().addDataPath(root.string());
        mcp::McpServer server(resources);

        const json out = callTool(server, "mounts", json::object());
        CHECK(out["rpuVersion"] == "v2.x.x");
        // Unpackaged source is a development line and not any release; say so rather than leave it
        // to be mistaken for one.
        CHECK(out.contains("rpuVersionNote"));

        fs::remove_all(root);
    }

    SECTION("a .dat mount is reported as one, with its size") {
        // gitDescribe used to answer this and could say nothing about a .dat, which is the mount
        // whose version actually matters. Reading the data covers both kinds.
        resource::GameResources resources;
        resources.files().addDataPath(test::dataPath("f2_res.dat").string());
        mcp::McpServer server(resources);

        const json out = callTool(server, "mounts", json::object());
        REQUIRE(out["mounts"].size() == 1);
        CHECK(out["mounts"][0]["kind"] == "dat");
        CHECK(out["mounts"][0]["bytes"] > 0);
        CHECK_FALSE(out.contains("failedMounts"));
    }

    SECTION("a data path that does not exist is reported, not skipped") {
        resource::GameResources resources;
        resources.files().addDataPath("/gecko-test/definitely/not/here.dat");
        mcp::McpServer server(resources);

        const json out = callTool(server, "mounts", json::object());
        REQUIRE(out.contains("failedMounts"));
        REQUIRE(out["failedMounts"].size() == 1);
        CHECK(out["failedMounts"][0]["reason"] == "does not exist");
    }
}

namespace {

// The critter layout fallout2-ce protoRead + protoCritterDataRead expect. Base stat i holds
// 100 + i and bonus stat i holds 1000 + i, so a reported figure names both slots it came from;
// skill i holds 50 + i. Built by hand so the test pins the engine's layout rather than gecko
// agreeing with itself.
test::ByteWriter critterProto() {
    test::ByteWriter w;
    for (const std::int32_t v : { 0x01000001, 200, 0x01000040, 0, 0, 0x20000000, 0x6000, 0x04000005, -1, 77, 1 }) {
        w.be32(static_cast<std::uint32_t>(v));
    }
    w.be32(0x0802);
    for (std::int32_t stat = 0; stat < 35; ++stat) {
        w.be32(static_cast<std::uint32_t>(100 + stat));
    }
    for (std::int32_t stat = 0; stat < 35; ++stat) {
        w.be32(static_cast<std::uint32_t>(1000 + stat));
    }
    for (std::int32_t skill = 0; skill < 18; ++skill) {
        w.be32(static_cast<std::uint32_t>(50 + skill));
    }
    for (const std::int32_t v : { 1, 150, 3, 2 }) {
        w.be32(static_cast<std::uint32_t>(v));
    }
    return w;
}

} // namespace

// proto_info reports effective figures, base plus bonus. Base alone reads a Turret at 30 hit points
// against the 75 the engine gives it, and every critter in the game as resisting nothing, because a
// critter's resistances live entirely in the bonus arrays.
TEST_CASE("proto_info reports a critter's effective stats and skills", "[mcp][proto_info]") {
    const fs::path root = fs::temp_directory_path() / "gecko_proto_info";
    fs::remove_all(root);
    writeText(root / "proto/critters/critters.lst", "critter.pro\n");
    writeText(root / "text/english/game/pro_crit.msg", "{200}{}{Test Critter}\n");
    writeBytes(root / "proto/critters/critter.pro", critterProto());

    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    mcp::McpServer server(resources);

    const json out = callTool(server, "proto_info", { { "pid", 0x01000001 } });
    const json entry = out.is_array() ? out[0] : out;
    REQUIRE(entry.contains("critter"));
    const json& critter = entry["critter"];

    SECTION("stats are base plus bonus, not base") {
        // Base block: special[0..6], maxHitPoints@7, actionPoints@8, armorClass@9.
        CHECK(critter["hitPoints"] == 107 + 1007);
        CHECK(critter["armorClass"] == 109 + 1009);
    }

    SECTION("every skill is reported, in Skill enum order") {
        REQUIRE(critter.contains("skills"));
        // Indexed positionally: skill_defs.h opens with SKILL_INVALID = -1, so the enum's values sit
        // one below their slot and using them would shift every skill by one.
        CHECK(critter["skills"]["small_guns"] == 50);
        CHECK(critter["skills"]["outdoorsman"] == 50 + 17);
        CHECK(critter["skills"].size() == 18);
    }

    SECTION("the DR bonuses straddle the two arrays") {
        // The base block runs DT[7] at slots 17..23 then DR[9] at 24..32, but the bonus block packs
        // its 16 resistance words 8 + 8: bonusDT[8] at 17..24, bonusDR[8] at 25..32. So the
        // normal-damage DR bonus is the LAST word of bonusDamageThreshold -- slot 24, value 1024 --
        // and not the first word of bonusDamageResistance, slot 25.
        REQUIRE(critter.contains("damageResistance"));
        CHECK(critter["damageThreshold"]["normal"] == 117 + 1017);
        CHECK(critter["damageResistance"]["normal"] == 124 + 1024);
        // Reading the two arrays index-for-index is the bug this guards: it would shift every
        // resistance one slot and give 1025 here, which looks just as plausible.
        CHECK(critter["damageResistance"]["normal"] != 124 + 1025);
    }

    fs::remove_all(root);
}

// dump_grid's name filter exists because asking where one object sits meant reading the whole map:
// epax is 2684 objects, nearly all scroll blockers, so three rows cost 445KB.
TEST_CASE("dump_grid filters objects by name", "[cli][dump_grid]") {
    StubProvider provider;
    auto mapFile = Map::createEmptyMapFile();
    for (int i = 0; i < 5; ++i) {
        // Critters, not scenery: scenery dereferences its Pro in both directions, so reading the
        // map back without mounted protos fails. Critters carry their pid and nothing else.
        const uint32_t pid = pidOf(Pro::OBJECT_TYPE::CRITTER, static_cast<uint32_t>(40 + i));
        auto object = std::make_shared<MapObject>();
        object->pro_pid = pid;
        object->elevation = 0;
        object->position = 100 * 200 + 100 + i;
        mapFile.map_objects[0].push_back(object);
    }

    Map map{ "synthetic_filter.map" };
    map.setMapFile(std::make_unique<Map::MapFile>(std::move(mapFile)));
    TempFile out{ "geck_dump_grid_filter", ".map" };
    {
        MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(out.path());
        REQUIRE(writer.write(map.getMapFile()));
    }

    resource::GameResources resources;
    const auto dump = [&](const std::string& nameFilter) {
        cli::DumpGridOptions options;
        options.map = out.path().string();
        options.floor = false;
        options.nameFilter = nameFilter;
        std::ostringstream oss;
        REQUIRE(cli::dumpMapGrid(resources, options, oss) == 0);
        return json::parse(oss.str())["elevations"][0]["objects"];
    };

    const json all = dump("");
    REQUIRE(all.size() == 5);

    SECTION("a pattern that matches nothing returns nothing, not everything") {
        CHECK(dump("definitely-not-a-proto-name").empty());
    }

    SECTION("an unparsable pattern is ignored rather than fatal") {
        // A filter on a dump: refusing to answer is worse than answering unfiltered.
        CHECK(dump("[unclosed").size() == all.size());
    }

    SECTION("the filter matches the name each object actually reports") {
        // Whatever the names resolve to without mounted data, filtering on one of them must select
        // at least that object and never more than the unfiltered set.
        const std::string first = all[0]["name"].get<std::string>();
        const json some = dump(first);
        CHECK_FALSE(some.empty());
        CHECK(some.size() <= all.size());
    }
}

namespace {

// The item header fallout2-ce protoRead reads: pid, messageId, fid, lightDistance, lightIntensity,
// flags, extendedFlags, sid, type, material, size, weight, cost, inventoryFid, then a byte of
// soundId. By hand, so the test pins the engine's layout rather than gecko agreeing with itself.
test::ByteWriter itemHeader(std::int32_t pid, std::int32_t messageId, std::int32_t type) {
    test::ByteWriter w;
    for (const std::int32_t v : { pid, messageId, 0x07000010, 0, 0, 0x00000008, 0x00000376, -1, type, 1, 3, 7, 250,
             0x07000020 }) {
        w.be32(static_cast<std::uint32_t>(v));
    }
    w.u8('0');
    return w;
}

} // namespace

// The weapon block is what turned "which weapons carry Long Range?" from opening .pro files by hand
// into one call — and getting the perk wrong is quiet, because a wrong perk name reads perfectly.
TEST_CASE("proto_info reports a weapon's perk and numbers", "[mcp][proto_info]") {
    const fs::path root = fs::temp_directory_path() / "gecko_proto_weapon";
    fs::remove_all(root);
    writeText(root / "proto/items/items.lst", "weapon.pro\n");
    writeText(root / "text/english/game/pro_item.msg", "{100}{}{Test Rifle}\n{101}{}{A rifle.}\n");
    writeText(root / "text/english/game/proto.msg", "{101}{}{Metal}\n{251}{}{laser}\n{308}{}{10mm}\n");
    writeText(root / "text/english/game/perk.msg", "{159}{}{Long Range}\n");

    // animationCode, damageMin 10, damageMax 20, damageType laser, rangePrimary 25,
    // rangeSecondary 20, projectile, minStrength 4, actionCostPrimary 5, actionCostSecondary 6,
    // critFail, perk 58 (Weapon Long Range), burstRounds 10, ammoType, ammoPid, ammoCapacity 30.
    test::ByteWriter weapon = itemHeader(1, 100, 3);
    for (const std::int32_t v : { 6, 10, 20, 1, 25, 20, -1, 4, 5, 6, 2, 58, 10, 8, 3, 30 }) {
        weapon.be32(static_cast<std::uint32_t>(v));
    }
    weapon.u8('A');
    REQUIRE(weapon.size() == 122);
    writeBytes(root / "proto/items/weapon.pro", weapon);

    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    mcp::McpServer server(resources);

    const json out = callTool(server, "proto_info", { { "pid", 1 } });
    const json entry = out.is_array() ? out[0] : out;
    REQUIRE(entry.contains("weapon"));
    const json& w = entry["weapon"];

    // perk_defs.h opens with PERK_INVALID = -1, so every perk's value sits one below its line
    // position. Numbering from the first line instead put shotguns under Long Range once, and the
    // result looked entirely plausible.
    CHECK(w["perk"] == "long_range");
    CHECK(w["damageMin"] == 10);
    CHECK(w["damageMax"] == 20);
    CHECK(w["rangePrimary"] == 25);
    CHECK(w["rangeSecondary"] == 20);
    CHECK(w["actionCostPrimary"] == 5);
    CHECK(w["actionCostSecondary"] == 6);
    CHECK(w["minimumStrength"] == 4);
    CHECK(w["burstRounds"] == 10);
    CHECK(w["ammoCapacity"] == 30);

    fs::remove_all(root);
}
