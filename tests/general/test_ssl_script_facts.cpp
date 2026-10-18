#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ssl/ScriptFacts.h"

using namespace geck::ssl;
using Field = ScriptFacts::Override::Field;

namespace {

// The parts of RP headers/define.h and command.h the scripts below rely on.
constexpr const char* DEFINE_H = R"(
#define TRAIT_OBJECT      (1)
#define OBJECT_AI_PACKET  (5)
#define OBJECT_TEAM_NUM   (6)
)";
constexpr const char* COMMAND_H = R"(
#define set_team(cr,team)       critter_add_trait(cr,TRAIT_OBJECT,OBJECT_TEAM_NUM,team)
#define set_self_team(team)     set_team(self_obj,team)
#define set_ai(cr,ai)           critter_add_trait(cr,TRAIT_OBJECT,OBJECT_AI_PACKET,ai)
#define set_self_ai(ai)         set_ai(self_obj,ai)
)";

MemorySourceProvider rpTree(const std::string& script) {
    MemorySourceProvider files;
    files.add("headers/define.h", DEFINE_H);
    files.add("headers/command.h", COMMAND_H);
    files.add("headers/teams.h", "#define TEAM_ARROYO (1)\n");
    files.add("headers/aipacket.h", "#define AI_ARROYO_VILLAGER (2)\n");
    files.add("headers/modreact.h", "#define LVAR_reaction (0)\n#define LVAR_reaction_level (1)\n");
    files.add("arroyo/script.ssl",
        "#include \"..\\headers\\define.h\"\n#include \"..\\headers\\command.h\"\n#include \"..\\headers\\teams.h\"\n"
        "#include \"..\\headers\\aipacket.h\"\n#include \"..\\headers\\modreact.h\"\n"
            + script);
    return files;
}

ScriptFacts inspect(const std::string& script) {
    const auto files = rpTree(script);
    return readFacts(ScriptUnit::load(files, "arroyo/script.ssl"));
}

} // namespace

TEST_CASE("readFacts reads the team and AI packet a script sets on itself, with their values", "[ssl][facts]") {
    // RP arroyo/acvillgr.ssl: every villager is given the villager packet on each map entry.
    const auto facts = inspect(R"ssl(
procedure map_enter_p_proc;
procedure map_enter_p_proc begin
   set_self_team(TEAM_ARROYO);
   set_self_ai(AI_ARROYO_VILLAGER);
end
)ssl");
    CHECK(facts.problems.empty());
    REQUIRE(facts.overrides.size() == 2);
    const auto& team = facts.overrides[0];
    CHECK(team.field == Field::Team);
    CHECK(team.procedure == "map_enter_p_proc");
    CHECK(team.value == 1);
    CHECK(team.valueName == "TEAM_ARROYO");
    const auto& ai = facts.overrides[1];
    CHECK(ai.field == Field::AiPacket);
    CHECK(ai.value == 2);
    CHECK(ai.valueName == "AI_ARROYO_VILLAGER");
    CHECK(ai.file == facts.script);
    CHECK(ai.line == 10); // the five #includes, the blank line, then the procedure
}

TEST_CASE("readFacts finds the opcode through any macro, and only where it runs", "[ssl][facts]") {
    const auto facts = inspect(R"ssl(
#define become_hostile set_ai(self_obj, 7)
#define unused_helper set_self_team(3)
procedure talk_p_proc begin
   //set_self_ai(9);
   /* critter_add_trait(self_obj, TRAIT_OBJECT, OBJECT_AI_PACKET, 9); */
   display_msg("set_self_ai(9)");
   set_ai(dude_obj, 9);
   if (hostile) then begin
      become_hostile;
   end
   CRITTER_ADD_TRAIT(SELF_OBJ, TRAIT_OBJECT, OBJECT_TEAM_NUM, 4);
end
)ssl");
    REQUIRE(facts.overrides.size() == 2);
    CHECK(facts.overrides[0].field == Field::AiPacket); // through the script's own macro
    CHECK(facts.overrides[0].value == 7);
    CHECK(facts.overrides[0].procedure == "talk_p_proc");
    CHECK(facts.overrides[1].field == Field::Team); // SSL names are case-insensitive
    CHECK(facts.overrides[1].value == 4);
}

TEST_CASE("readFacts attributes a call to the procedure it is in", "[ssl][facts]") {
    const auto facts = inspect(R"ssl(
procedure helper(variable who) begin
   if (who) then begin
      set_self_team(5);
   end
end
procedure map_enter_p_proc begin
   set_self_ai(6);
   call helper(1);
end
)ssl");
    REQUIRE(facts.overrides.size() == 2);
    CHECK(facts.overrides[0].procedure == "helper");
    CHECK(facts.overrides[1].procedure == "map_enter_p_proc");
}

TEST_CASE("readFacts ignores code the compiled script does not run", "[ssl][facts]") {
    // sslc drops a procedure nothing calls, and the branch of an `if` on a constant that can't run.
    const auto facts = inspect(R"ssl(
#define USE_BERSERK false
procedure unused begin
   set_self_team(5);
end
procedure map_enter_p_proc begin
   if (USE_BERSERK) then set_self_ai(7);
   else set_self_ai(6);
end
)ssl");
    REQUIRE(facts.overrides.size() == 1);
    CHECK(facts.overrides[0].value == 6);
}

TEST_CASE("readFacts still recognises the command.h aliases when the headers are missing", "[ssl][facts]") {
    MemorySourceProvider files;
    files.add("script.ssl", "#include \"../headers/command.h\"\nprocedure map_enter_p_proc begin\n set_self_ai(AI_X);\nend\n");
    const auto facts = readFacts(ScriptUnit::load(files, "script.ssl"));
    REQUIRE(facts.overrides.size() == 1);
    CHECK(facts.overrides[0].field == Field::AiPacket);
    CHECK_FALSE(facts.overrides[0].value.has_value());
    REQUIRE(facts.problems.size() == 1);
    CHECK(facts.problems[0] == "script.ssl:1: cannot open include file ../headers/command.h");
}

TEST_CASE("readFacts names local variables from the script and the headers it includes", "[ssl][facts]") {
    // RP: 560 scripts take LVAR 0-3 from modreact.h or doors_containers.h and number their own from 4.
    const auto facts = inspect(R"ssl(
#define LVAR_Herebefore                 (4)
#define LVAR_Hostile                    (5)
#define LVAR_reaction_mine              (1)
// #define LVAR_Commented                  (6)
)ssl");
    REQUIRE(facts.localVarNames.size() == 4);
    CHECK(facts.localVarNames.at(0) == "LVAR_reaction");
    CHECK(facts.localVarNames.at(1) == "LVAR_reaction_mine"); // the script's own name wins
    CHECK(facts.localVarNames.at(4) == "LVAR_Herebefore");
    CHECK(facts.localVarNames.at(5) == "LVAR_Hostile");
}
