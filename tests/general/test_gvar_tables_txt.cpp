#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <ostream>
#include <string>

#include "cli/Endings.h"
#include "cli/GlobalVars.h"
#include "cli/Party.h"
#include "format/holodisk/HolodiskTxt.h"
#include "format/karma/KarmaVarTxt.h"
#include "format/party/PartyTxt.h"
#include "reader/holodisk/HolodiskTxtReader.h"
#include "reader/karma/KarmaVarTxtReader.h"
#include "reader/party/PartyTxtReader.h"
#include "resource/GameResources.h"

#include "support/CliJson.h"
#include "support/TempFile.h"

using namespace geck;
namespace fs = std::filesystem;
using geck::test::writeFile;

namespace {

template <typename Build>
nlohmann::json run(const fs::path& root, Build build) {
    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    return test::runJson([&](std::ostream& out) { return build(resources, out); });
}

} // namespace

TEST_CASE("parseHolodiskTxt and parseKarmaVarTxt read the engine's flat gvar tables", "[gvar_tables_txt]") {
    const HolodiskTxt holodisks = parseHolodiskTxt(std::string{ "# gvar, title, text\n#214, 400, 1000\n220, 401, 2000\n221 402\n" });
    REQUIRE(holodisks.holodisks.size() == 1); // the commented row and the short row are skipped
    CHECK(holodisks.holodisks[0].gvar == 220);
    CHECK(holodisks.holodisks[0].title == 401);
    CHECK(holodisks.holodisks[0].text == 2000);

    const KarmaVarTxt karma = parseKarmaVarTxt(std::string{ "  # GVAR_PLAYER_REPUTATION\n0, 48, 1000, 1100\n3\t49 1001,1101\n" });
    REQUIRE(karma.entries.size() == 2);
    CHECK(karma.entries[1].gvar == 3);
    CHECK(karma.entries[1].art == 49);
    CHECK(karma.entries[1].name == 1001);
    CHECK(karma.entries[1].description == 1101);
}

TEST_CASE("parsePartyTxt reads members up to the first missing index", "[gvar_tables_txt]") {
    const PartyTxt party = parsePartyTxt(std::string{ "[Party Member 0]   ; Player\n"
                                                      "party_member_pid=16777216\n"
                                                      "level_pids=-1\n"
                                                      "[Party Member 1]   ; pMMyron_PID\n"
                                                      "party_member_pid=16777376\n"
                                                      "Distance=Stay_Close, charge\n"
                                                      "level_minimum=6\n"
                                                      "level_up_every=4\n"
                                                      "level_pids=16777541,16777542\n"
                                                      "[Party Member 2]\n"
                                                      "level_minimum=3\n"
                                                      "level_pids=1,2\n"
                                                      "[Party Member 3]\n"
                                                      "party_member_pid=16777300\n" });

    // Member 2 has no party_member_pid, so the engine stops there and never reaches member 3.
    REQUIRE(party.members.size() == 2);
    const PartyMember& myron = party.members[1];
    CHECK(myron.index == 1);
    CHECK(myron.pid == 16777376);
    CHECK(myron.distance == std::vector<std::string>{ "stay_close", "charge" });
    CHECK(myron.levelMinimum == 6);
    CHECK(myron.levelUpEvery == 4);
    CHECK(myron.levelPids == std::vector<int>{ 16777541, 16777542 });
    // Without level_up_every the engine reads no level fields.
    CHECK(party.members[0].levelPids.empty());
}

TEST_CASE("parsePartyTxt ignores level fields without level_up_every", "[gvar_tables_txt]") {
    const PartyTxt party = parsePartyTxt(std::string{ "[Party Member 0]\nparty_member_pid=1\nlevel_minimum=3\nlevel_pids=5,6\n" });
    REQUIRE(party.members.size() == 1);
    CHECK(party.members[0].levelMinimum == 0);
    CHECK(party.members[0].levelPids.empty());
}

TEST_CASE("The endings, gvars and party tools surface the engine tables", "[gvar_tables_txt][cli]") {
    const test::TempDir rootDir{ "gvar_tables_data" };
    const fs::path& root = rootDir.path();
    writeFile(root / "data" / "vault13.gam", "GAME_GLOBAL_VARS:\nGVAR_REP :=0;\nGVAR_DISK :=0;\nGVAR_END :=0;\n");
    writeFile(root / "data" / "karmavar.txt", "0, 48, 1000, 1100\n");
    writeFile(root / "data" / "holodisk.txt", "1, 401, 2000\n");
    writeFile(root / "data" / "endgame.txt", "2, 1, 440, nar_a\n");
    writeFile(root / "data" / "enddeath.txt", "-1, 0, -1, 0, 0, 20, nar_d\n2, 3, 1, -1, 5, 40, nar_e\n");
    writeFile(root / "data" / "party.txt", "[Party Member 0]\nparty_member_pid=16777216\n[Party Member 1]\nparty_member_pid=16777376\n"
                                           "chem_use=clean\nlevel_up_every=4\nlevel_pids=16777541,-1\n");
    writeFile(root / "text" / "english" / "game" / "editor.msg", "{1000}{}{Saint}\n{1100}{}{Very good.}\n");
    writeFile(root / "text" / "english" / "game" / "pipboy.msg", "{401}{}{Disk One}\n");
    writeFile(root / "text" / "english" / "game" / "map.msg", "{1501}{}{Klamath}\n");
    writeFile(root / "text" / "english" / "cuts" / "nar_e.txt", "1:You fell.\n2:The end.\n");

    SECTION("endings adds the death endings with the engine's conditions and subtitles") {
        const auto j = run(root, cli::buildEndings);
        REQUIRE(j["deathEndings"].size() == 2);
        CHECK(j["deathEndings"][0]["condition"].is_null()); // gvar -1: no gvar check
        const auto& death = j["deathEndings"][1];
        CHECK(death["condition"] == "GVAR_END < 3"); // dropped once the gvar reaches the value
        CHECK(death["areaKnown"]["index"] == 1);
        CHECK(death["areaKnown"]["name"] == "Klamath");
        CHECK(death["areaNotKnown"].is_null());
        CHECK(death["subtitles"] == nlohmann::json::array({ "You fell.", "The end." }));
        CHECK(j["stats"]["deathEndings"] == 2);
    }

    SECTION("gvars lists each gvar's roles from the data files") {
        const auto j = run(root, cli::buildGlobalVars);
        CHECK(j["gvars"][0]["roles"][0]["kind"] == "karma");
        CHECK(j["gvars"][0]["roles"][0]["title"] == "Saint");
        CHECK(j["gvars"][1]["roles"][0]["kind"] == "holodisk");
        CHECK(j["gvars"][1]["roles"][0]["title"] == "Disk One");
        REQUIRE(j["gvars"][2]["roles"].size() == 2);
        CHECK(j["gvars"][2]["roles"][0]["kind"] == "ending");
        CHECK(j["gvars"][2]["roles"][1]["kind"] == "deathEnding");
        CHECK(j["gvars"][2]["roles"][1]["droppedAt"] == 3);
    }

    SECTION("party leaves out the player and the -1 level placeholder") {
        const auto j = run(root, cli::buildParty);
        REQUIRE(j["members"].size() == 1);
        CHECK(j["members"][0]["pid"] == 16777376);
        CHECK(j["members"][0]["options"]["chemUse"] == nlohmann::json::array({ "clean" }));
        REQUIRE(j["members"][0]["levels"].size() == 1);
        CHECK(j["members"][0]["levels"][0]["pid"] == 16777541);
    }
}
