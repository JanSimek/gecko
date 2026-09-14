#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cli/SaveInspect.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "writer/map/MapWriter.h"

#include "support/ByteWriter.h"
#include "support/GzipBytes.h"
#include "support/MapObjectBuilder.h"
#include "support/ProStubProvider.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t DUDE_PID = 0x01000000;
constexpr int32_t DUDE_OBJECT_ID = 900;

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

void fixedString(ByteWriter& w, const std::string& text, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        w.u8(i < text.size() ? static_cast<uint8_t>(text[i]) : 0);
    }
}

// The player's object record: the 22-field common block, then the critter tail in engine order.
void dudeRecord(ByteWriter& w) {
    const std::vector<uint32_t> common{ DUDE_OBJECT_ID, 18084, 0, 0, 0, 0, 0, 0, 0x01000000, 0, 0, DUDE_PID, 0, 0, 0, 0,
        static_cast<uint32_t>(-1), static_cast<uint32_t>(-1), 0, 0, 0, 0 };
    for (const uint32_t v : common) {
        w.be32(v);
    }
    for (const uint32_t v : { 28u, 1u, 10u, 0u, 0u, 0u, 2u, 25u, 0u, 0u }) {
        w.be32(v);
    }
}

// SAVE.DAT in fallout2-ce loadsave.cc master_save_list order, up to and including the combat block.
std::vector<uint8_t> buildSaveDat(const std::vector<int32_t>& globals) {
    ByteWriter w;
    fixedString(w, "FALLOUT SAVE FILE", 24);
    w.be16(1).be16(2).u8(0);
    fixedString(w, "Tester", 32);
    fixedString(w, "combat deadlock", 30);
    w.be16(14).be16(9).be16(2026).be32(0);    // file date, time
    w.be16(7).be16(25).be16(2241).be32(1234); // game date, time
    w.be16(0).be16(75);                       // elevation, map index
    fixedString(w, "TEST.SAV", 16);
    w.fill(0, 224 * 133).fill(0, 128);

    w.be32(0); // player combat id
    for (const int32_t g : globals) {
        w.be32(static_cast<uint32_t>(g));
    }
    w.be32(1);
    fixedString(w, "TEST.SAV", 9); // NUL-terminated
    w.be32(10);                    // AUTOMAP.DB size
    for (const int32_t g : globals) {
        w.be32(static_cast<uint32_t>(g));
    }

    dudeRecord(w);
    w.be32(18488); // view centre
    w.be32(0);     // _sneak_working
    w.be32(0);     // proto flags
    w.fill(0, (35 + 35 + 18) * 4);
    w.be32(0).be32(1500).be32(0).be32(0); // body type, experience, kill type, damage type
    w.fill(0, 19 * 4);                    // kills
    w.be32(0).be32(9).be32(14).be32(static_cast<uint32_t>(-1));
    for (int perk = 0; perk < 119; ++perk) { // one party member row
        w.be32(perk == 5 ? 1 : 0);
    }

    w.be32(3); // IN_COMBAT | PLAYER_TURN
    w.be32(0).be32(0).be32(550);
    w.be32(2).be32(1).be32(3); // 2 combatants + 1 non-combatant
    w.be32(0);                 // player cid
    w.be32(0).be32(1).be32(2); // combat list order
    for (int i = 0; i < 3; ++i) {
        const int32_t lastTarget = i == 1 ? DUDE_OBJECT_ID : -1;
        w.be32(static_cast<uint32_t>(-1)).be32(static_cast<uint32_t>(lastTarget)).be32(static_cast<uint32_t>(-1)).be32(0);
    }
    return w.data();
}

void writeSlotMap(const fs::path& slot) {
    StubProvider provider;
    auto mapFile = Map::createEmptyMapFile();
    mapFile.header.flags |= 0x01;
    for (int32_t cid : { 1, 2 }) {
        auto critter = std::make_shared<MapObject>();
        fillBase(*critter, cid);
        critter->pro_pid = pidOf(Pro::OBJECT_TYPE::CRITTER, 20);
        critter->elevation = 0;
        critter->critter_index = cid;
        critter->position = 17000 + cid;
        critter->group_id = 121;
        critter->current_ap = 7;
        critter->maneuver = 0x01;
        critter->who_hit_me = static_cast<uint32_t>(-1);
        mapFile.map_objects[0].push_back(critter);
    }
    TempFile plain{ "geck_save_slot_map", ".map" };
    {
        MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(plain.path());
        REQUIRE(writer.write(mapFile));
    }
    writeAllBytes(slot / "TEST.SAV", gzipBytes(readAllBytes(plain.path())));
}

json describe(const fs::path& dataRoot, const fs::path& slot, int& rc, std::string& text) {
    resource::GameResources resources;
    resources.files().addDataPath(dataRoot.string());
    cli::DescribeSaveOptions options;
    options.slotPath = slot.string();
    std::ostringstream out;
    rc = cli::describeSave(resources, options, out);
    text = out.str();
    return rc == 0 ? json::parse(text) : json();
}

} // namespace

TEST_CASE("describeSave decodes a slot and joins the combat list to the slot map", "[cli][save]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "save_inspect";
    fs::remove_all(root);
    const fs::path data = root / "gamedata";
    writeText(data / "data" / "vault13.gam", "GAME_GLOBAL_VARS:\nGVAR_A :=0;\nGVAR_B :=5;\n");
    writeText(data / "data" / "party.txt", "[Party Member 0]\nparty_member_pid=16777216\n");
    const fs::path slot = root / "SLOT01";
    writeAllBytes(slot / "SAVE.DAT", buildSaveDat({ 0, 7 }));
    writeSlotMap(slot);

    int rc = 0;
    std::string text;
    const json save = describe(data, slot, rc, text);
    INFO(text);
    REQUIRE(rc == 0);

    CHECK(save.at("header").at("characterName") == "Tester");
    CHECK(save.at("header").at("mapFile") == "TEST.SAV");
    CHECK(save.at("globals").at("changed") == json::array({ { { "index", 1 }, { "name", "GVAR_B" }, { "value", 7 }, { "default", 5 } } }));
    CHECK(save.at("dude").at("hex") == 18084);
    CHECK(save.at("dude").at("ap") == 10);
    CHECK(save.at("dude").at("whoHitMeCid") == 2);
    CHECK(save.at("dude").at("experience") == 1500);
    CHECK(save.at("perks") == json::array({ { { "perk", 5 }, { "rank", 1 } } }));
    CHECK(save.at("currentMap").at("loaded") == true);

    const auto& combat = save.at("combat");
    CHECK(combat.at("inCombat") == true);
    CHECK(combat.at("playerTurn") == true);
    CHECK(combat.at("combatants") == 2);
    const auto& list = combat.at("list");
    REQUIRE(list.size() == 3);
    CHECK(list[0].at("critter").at("who") == "dude");
    CHECK(list[1].at("partition") == "combatant");
    CHECK(list[1].at("critter").at("hex") == 17001);
    CHECK(list[1].at("critter").at("team") == 121);
    CHECK(list[1].at("aiInfo").at("lastTarget").at("objectId") == DUDE_OBJECT_ID);
    CHECK(list[1].at("aiInfo").at("lastTarget").at("hex") == 18084);
    CHECK(list[2].at("partition") == "noncombatant");
    CHECK(list[2].at("critter").at("hex") == 17002);
    CHECK(save.at("parsedBytes") == save.at("totalBytes"));
}

TEST_CASE("describeSave reports a vault13.gam that does not match the save", "[cli][save]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "save_inspect_mismatch";
    fs::remove_all(root);
    const fs::path data = root / "gamedata";
    writeText(data / "data" / "vault13.gam", "GAME_GLOBAL_VARS:\nGVAR_A :=0;\nGVAR_B :=5;\nGVAR_C :=0;\n");
    writeText(data / "data" / "party.txt", "[Party Member 0]\nparty_member_pid=16777216\n");
    const fs::path slot = root / "SLOT01";
    writeAllBytes(slot / "SAVE.DAT", buildSaveDat({ 0, 7 }));

    int rc = 0;
    std::string text;
    describe(data, slot, rc, text);
    CHECK(rc != 0);
    CHECK(text.find("vault13.gam") != std::string::npos);
}
