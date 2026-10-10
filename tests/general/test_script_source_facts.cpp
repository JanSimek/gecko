#include <catch2/catch_test_macros.hpp>

#include "resource/ScriptSourceFacts.h"

using geck::resource::inspectScriptSource;
using Field = geck::resource::ScriptSourceFacts::Override::Field;

TEST_CASE("inspectScriptSource finds the AI packet a critter script sets on itself", "[script][source]") {
    // Shape of RP vault13/ocgrutha.ssl: the packet is reassigned on every map entry.
    const auto facts = inspectScriptSource(R"(
procedure map_enter_p_proc;
procedure map_enter_p_proc begin
   set_self_team(TEAM_VAULT13);
   set_self_ai(AI_VAULT_DEATHCLAW);
end
)");
    REQUIRE(facts.overrides.size() == 2);
    CHECK(facts.overrides[0].field == Field::Team);
    CHECK(facts.overrides[1].field == Field::AiPacket);
    CHECK(facts.overrides[1].procedure == "map_enter_p_proc");
    CHECK(facts.overrides[1].line == 5);
    CHECK(facts.assigns(Field::AiPacket));
}

TEST_CASE("inspectScriptSource ignores the commented-out template lines", "[script][source]") {
    // RP brokhill/hcchuck.ssl keeps these as comments; they do nothing in game.
    const auto facts = inspectScriptSource(R"(
procedure map_enter_p_proc begin
   //critter_add_trait(self_obj,TRAIT_OBJECT,OBJECT_TEAM_NUM,TEAM_BROKEN_HILLS);
   /* critter_add_trait(self_obj,TRAIT_OBJECT,OBJECT_AI_PACKET,AI_COWARD); */
   display_msg("// set_self_ai(1) is just text");
end
)");
    CHECK(facts.overrides.empty());
}

TEST_CASE("inspectScriptSource reports the raw opcode and runtime changes by procedure", "[script][source]") {
    const auto facts = inspectScriptSource(R"(
procedure talk_p_proc begin
   if (hostile) then begin
      critter_add_trait(self_obj, TRAIT_OBJECT, OBJECT_AI_PACKET, AI_BERSERK);
   end
   set_team(self_obj, TEAM_PLAYER);
   set_ai(other_obj, AI_COWARD);
end
)");
    REQUIRE(facts.overrides.size() == 2);
    CHECK(facts.overrides[0].field == Field::AiPacket);
    CHECK(facts.overrides[0].procedure == "talk_p_proc");
    CHECK(facts.overrides[1].field == Field::Team);
}

TEST_CASE("inspectScriptSource skips macro definitions outside any procedure", "[script][source]") {
    const auto facts = inspectScriptSource(R"(
#define set_self_ai(ai) critter_add_trait(self_obj,TRAIT_OBJECT,OBJECT_AI_PACKET,ai)
procedure start begin
end
)");
    CHECK(facts.overrides.empty());
}

TEST_CASE("inspectScriptSource names local variables from LVAR defines", "[script][source]") {
    const auto facts = inspectScriptSource(R"(
#define LVAR_Herebefore                 (0)
#define LVAR_Hostile                    (1)
#define LVAR_Personal_Enemy 2
// #define LVAR_Commented                  (3)
)");
    REQUIRE(facts.localVarNames.size() == 3);
    CHECK(facts.localVarNames.at(0) == "LVAR_Herebefore");
    CHECK(facts.localVarNames.at(2) == "LVAR_Personal_Enemy");
}
