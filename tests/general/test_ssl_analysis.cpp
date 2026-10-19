#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <string>

#include "ssl/CallGraph.h"
#include "ssl/Corpus.h"
#include "ssl/VariableRefs.h"

#include "support/SslParse.h"

using namespace geck::ssl;
using geck::test::parseSsl;

TEST_CASE("CallGraph keeps what sslc compiles: entry points and what they reach", "[ssl_analysis][ssl]") {
    const ScriptUnit unit = parseSsl(R"ssl(
#define USE_OLD false
variable mode;
export procedure shared;
procedure shared begin end
procedure talk_p_proc begin
   call Node001;
   tvarCBack(@callback);
   if (USE_OLD) then call old_way;
   if (mode == 1) then call never_in_mode_0;
   if ((1 == 2) or (1 == 3)) then call kept_by_or;
end
procedure Node001 begin Node002(); end
procedure Node002 begin end
procedure callback begin end
procedure old_way begin end
procedure never_in_mode_0 begin end
procedure kept_by_or begin end
procedure orphan begin call Node002; end
procedure timer in 10 begin end
procedure shadows(variable Node003) begin Node003 := 1; end
procedure Node003 begin end
)ssl");
    REQUIRE(unit.diagnostics().empty());
    const CallGraph graph(unit.program());
    // kept_by_or: sslc does not fold `or`, so it keeps both branches of that `if`.
    for (const char* live : { "talk_p_proc", "node001", "node002", "callback", "shared", "timer", "kept_by_or" }) {
        INFO(live);
        CHECK(graph.isLive(live));
    }
    // old_way: behind a constant-false macro. never_in_mode_0: `mode` is never assigned, so it stays 0.
    // orphan: nothing calls it. Node003: only a parameter of that name is assigned.
    for (const char* dead : { "old_way", "never_in_mode_0", "orphan", "node003", "shadows" }) {
        INFO(dead);
        CHECK_FALSE(graph.isLive(dead));
    }
    // Calls on a dead branch are not references at all.
    CHECK(graph.references("TALK_P_PROC") == std::set<std::string>{ "callback", "kept_by_or", "node001" });
}

TEST_CASE("ConstantFolder folds literals, sslc's true/false and globals nothing assigns", "[ssl_analysis][ssl]") {
    const ScriptUnit unit = parseSsl(R"ssl(
variable fixed := 4;
variable changed;
import variable shared;
procedure start begin
   changed := 1;
   x := fixed * 2 + (3 if true else 9);
   y := changed;
   z := shared;
   w := 7 % 2;
   v := 1 and 0;
end
)ssl");
    REQUIRE(unit.diagnostics().empty());
    const ConstantFolder folder(unit.program());
    CHECK(folder.constants() == std::map<std::string, int64_t>{ { "fixed", 4 } });
    const auto& statements = unit.program().definition("start")->body->statements;
    const auto valueOf = [&](std::size_t i) { return folder.fold(*std::get<Assignment>(statements.at(i)->node).value); };
    CHECK(valueOf(1) == 11);
    CHECK_FALSE(valueOf(2).has_value()); // assigned in start
    CHECK_FALSE(valueOf(3).has_value()); // imported: another script may set it
    CHECK_FALSE(valueOf(4).has_value()); // sslc folds neither '%' nor and/or
    CHECK_FALSE(valueOf(5).has_value());
}

TEST_CASE("globalVariableRefs and localVariableRefs report each access with its number and macro", "[ssl_analysis][ssl]") {
    const ScriptUnit unit = parseSsl(R"ssl(
#define GVAR_SEED (12)
#define LVAR_Hostile (5)
#define set_seed(x) set_global_var(GVAR_SEED, x)
procedure map_enter_p_proc begin
   if (global_var(GVAR_SEED) == 0) then
      set_seed(1);
   set_local_var(LVAR_Hostile, local_var(LVAR_Hostile) + 1);
   x := global_var(GVAR_UNDEFINED);
end
)ssl");
    REQUIRE(unit.diagnostics().empty());
    const CallGraph graph(unit.program());

    const auto globals = globalVariableRefs(unit, graph);
    REQUIRE(globals.size() == 3);
    CHECK(globals[0].kind == VariableRef::Kind::Get);
    CHECK(globals[0].index == 12);
    CHECK(globals[0].name == "GVAR_SEED");
    CHECK(globals[1].kind == VariableRef::Kind::Set);
    CHECK(globals[1].location.line == 7); // where set_seed was written
    CHECK(globals[1].macros == std::vector<std::string>{ "set_seed" });
    CHECK_FALSE(globals[2].index.has_value());
    CHECK(globals[2].name == "GVAR_UNDEFINED");

    const auto locals = localVariableRefs(unit, graph);
    REQUIRE(locals.size() == 2);
    CHECK(locals[0].kind == VariableRef::Kind::Set);
    CHECK(locals[1].kind == VariableRef::Kind::Get);
    CHECK(locals[1].index == 5);
}

TEST_CASE("loadScripts reads every script once on worker threads and rethrows a failure", "[ssl_analysis][ssl]") {
    MemorySourceProvider files;
    std::vector<std::filesystem::path> paths;
    for (int i = 0; i < 40; ++i) {
        const std::string name = "s" + std::to_string(i) + ".ssl";
        files.add(name, "procedure start begin x := " + std::to_string(i) + "; end\n");
        paths.emplace_back(name);
    }
    std::vector<int> seen(paths.size(), -1);
    loadScripts(files, paths, [&seen](std::size_t index, const ScriptUnit& unit) {
        const auto& statement = unit.program().procedures.at(0).body->statements.at(0);
        seen[index] = static_cast<int>(std::get<IntLiteral>(std::get<Assignment>(statement->node).value->node).value);
    });
    for (std::size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i] == static_cast<int>(i));
    }

    std::atomic<int> visited{ 0 };
    CHECK_THROWS_AS(loadScripts(files, paths,
                        [&visited](std::size_t index, const ScriptUnit&) {
                            ++visited;
                            if (index == 3) {
                                throw std::runtime_error("visit failed");
                            }
                        }),
        std::runtime_error);
    CHECK(visited > 0);
}
