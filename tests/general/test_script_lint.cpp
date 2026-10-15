#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include "cli/ScriptIntrospect.h"
#include "cli/ScriptLint.h"
#include "cli/ScriptRefs.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "resource/GameResources.h"
#include "resource/ScriptLint.h"
#include "ssl/IntProgram.h"
#include "support/CompiledScript.h"

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

// A data tree with scripts.lst, one script's dialog .msg and its .ssl source.
fs::path makeTree(const std::string& name) {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / name;
    fs::remove_all(root);
    writeFile(root / "scripts" / "scripts.lst", "one.int ; first   # local_vars=2\ntwo.int ; second  # local_vars=0\n");
    writeFile(root / "text" / "english" / "dialog" / "one.msg", "{100}{}{Hello.}\n{101}{}{Bye.}\n");
    writeFile(root / "src" / "one.ssl",
        "#define NAME (1)\n" // SCRIPT_ONE: scripts.lst line 1
        "#define give(n) display_msg(message_str(NAME, n))\n"
        "procedure helper;\n"
        "procedure talk_p_proc begin\n"
        "   set_local_var(5, 1);\n"              // line 5: past the 2 scripts.lst gives
        "   x := local_var(1);\n"                // fine
        "   give(100);\n"                        // fine
        "   give(101);\n"                        // fine
        "   give(150);\n"                        // line 9: not in one.msg
        "   display_msg(message_str(9, 100));\n" // line 10: no scripts.lst line 9
        "   call helper;\n"
        "end\n"
        "procedure helper begin load_map(\"town.map\", 0); end\n" // line 13
        "procedure unused begin load_map(\"never.map\", 0); end\n");
    return root;
}

nlohmann::json run(const fs::path& root, const std::function<int(resource::GameResources&, std::ostream&)>& tool, int& rc) {
    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    std::ostringstream oss;
    rc = tool(resources, oss);
    return nlohmann::json::parse(oss.str());
}

} // namespace

TEST_CASE("lint_script reports local variables past scripts.lst's count, missing messages and unused code", "[script_lint]") {
    const fs::path root = makeTree("script_lint");
    cli::LintOptions options;
    options.name = "one";
    int rc = -1;
    const auto j = run(root, [&options](auto& r, auto& out) { return cli::lintScripts(r, options, out); }, rc);
    REQUIRE(rc == 0);
    CHECK(j["name"] == "one");
    CHECK(j["summary"]["errors"] == 3);
    CHECK(j["summary"]["notes"] == 3); // the unused procedure, and local variables 1 (never set) and 5 (never read)

    const auto& findings = j["findings"];
    const auto find = [&findings](const std::string& check, int line) {
        for (const auto& f : findings) {
            if (f["check"] == check && f["line"] == line) {
                return f;
            }
        }
        return nlohmann::json();
    };
    const auto localVar = find("local-vars", 5);
    REQUIRE_FALSE(localVar.is_null());
    CHECK(localVar["message"].get<std::string>().find("past the 2 local variables") != std::string::npos);
    CHECK_FALSE(find("messages", 9).is_null());
    CHECK(find("messages", 9)["message"] == "message 150 is not in dialog/one.msg; the engine shows \"Error\" instead");
    CHECK_FALSE(find("messages", 10).is_null());
    CHECK(find("unused-procedure", 14)["procedure"] == "unused");
    fs::remove_all(root);
}

TEST_CASE("lint_script all=true checks every script it can read and leaves notes out", "[script_lint]") {
    const fs::path root = makeTree("script_lint_all");
    cli::LintOptions options;
    options.all = true;
    options.includeNotes = false;
    int rc = -1;
    const auto j = run(root, [&options](auto& r, auto& out) { return cli::lintScripts(r, options, out); }, rc);
    REQUIRE(rc == 0);
    CHECK(j["scripts"] == 1); // two.int has neither a source nor a compiled script
    CHECK(j["unchecked"] == 1);
    CHECK(j["summary"]["errors"] == 3);
    CHECK_FALSE(j["summary"].contains("notes"));
    fs::remove_all(root);
}

TEST_CASE("describe_script analyze=true summarises the parsed script", "[script_lint]") {
    const fs::path root = makeTree("script_analyze");
    cli::DescribeScriptOptions options;
    options.name = "one";
    options.analyze = true;
    int rc = -1;
    const auto j = run(root, [&options](auto& r, auto& out) { return cli::describeScript(r, options, out); }, rc);
    REQUIRE(rc == 0);
    const auto& analysis = j["analysis"];
    REQUIRE(analysis.is_object());
    std::map<std::string, std::pair<bool, bool>> procedures; // name -> (live, entry point)
    for (const auto& p : analysis["procedures"]) {
        procedures[p["name"]] = { p["live"], p["entryPoint"] };
    }
    CHECK(procedures["talk_p_proc"] == std::pair{ true, true });
    CHECK(procedures["helper"] == std::pair{ true, false });
    CHECK(procedures["unused"] == std::pair{ false, false });
    CHECK(analysis["localVariables"]["count"] == 2);
    CHECK(analysis["localVariables"]["uses"].size() == 2);
    CHECK(analysis["messageUses"] == 4);
    CHECK(analysis["findings"].size() == 6);
    fs::remove_all(root);
}

TEST_CASE("script_refs finds calls, procedure references and macro uses in code that runs", "[script_lint]") {
    const fs::path root = makeTree("script_refs");
    int rc = -1;

    const auto calls = run(root, [](auto& r, auto& out) { return cli::findScriptRefs(r, { "call", "load_map" }, out); }, rc);
    REQUIRE(rc == 0);
    REQUIRE(calls["stats"]["refs"] == 1); // the one in `unused` never runs
    CHECK(calls["refs"][0]["procedure"] == "helper");
    CHECK(calls["refs"][0]["line"] == 13);

    const auto procedure = run(root, [](auto& r, auto& out) { return cli::findScriptRefs(r, { "procedure", "helper" }, out); }, rc);
    REQUIRE(procedure["stats"]["refs"] == 1);
    CHECK(procedure["refs"][0]["procedure"] == "talk_p_proc");

    const auto macro = run(root, [](auto& r, auto& out) { return cli::findScriptRefs(r, { "macro", "give" }, out); }, rc);
    CHECK(macro["stats"]["refs"] == 3); // lines 7, 8 and 9

    std::ostringstream bad;
    resource::GameResources resources;
    CHECK(cli::findScriptRefs(resources, { "nonsense", "x" }, bad) != 0);
    fs::remove_all(root);
}

TEST_CASE("localVarBlockProblems follows the engine's block sizes and finds overlaps and overruns", "[script_lint]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "lvar_blocks";
    fs::remove_all(root);
    writeFile(root / "scripts" / "scripts.lst", "a.int ; # local_vars=3\nb.int ; # local_vars=2\nc.int ; # local_vars=1\n");
    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    const Lst* lst = resources.repository().load<Lst>("scripts/scripts.lst");
    REQUIRE(lst != nullptr);

    Map map("test.map");
    map.setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
    auto& file = map.getMapFile();
    const auto place = [&file](uint32_t id, uint32_t program, uint32_t offset, uint32_t storedCount) {
        auto script = MapScript::makeObjectScript(MapScript::ScriptType::ITEM, id, program, id + 10);
        script.local_var_offset = offset;
        script.local_var_count = storedCount;
        file.map_scripts[static_cast<int>(MapScript::ScriptType::ITEM)].push_back(script);
        return script.pid;
    };
    const uint32_t a = place(0, 0, 0, 1);             // a.int: 3 per scripts.lst, so 0-2 (the stored 1 is ignored)
    const uint32_t b = place(1, 1, 2, 0);             // b.int: 2-3, overlaps a at 2
    const uint32_t c = place(2, 2, 4, 0);             // c.int: 4, clear
    place(3, 0, MapScript::NONE, 0);                  // no block yet: the engine allocates one
    file.map_local_vars = std::vector<int32_t>(4, 0); // so c.int's 4 is past the end

    auto problems = resource::localVarBlockProblems(map, *lst);
    REQUIRE(problems.size() == 3);
    CHECK(problems[a] == "Its local variables 0-2 overlap those of b.int, so they share them.");
    CHECK(problems[b] == "Its local variables 2-3 overlap those of a.int, so they share them.");
    CHECK(problems[c] == "Its local variables 4-4 run past the map's 4, so those fail to read and write.");

    // A saved game keeps each record's own count when it is set: a.int's 1 no longer reaches b.int.
    file.header.flags |= static_cast<uint32_t>(MapHeaderFlags::Saved);
    problems = resource::localVarBlockProblems(map, *lst);
    CHECK_FALSE(problems.contains(a));
    CHECK_FALSE(problems.contains(b));
    fs::remove_all(root);
}

TEST_CASE("lint_script checks global variables, timers, created objects and tiles", "[script_lint]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "script_lint_calls";
    fs::remove_all(root);
    writeFile(root / "scripts" / "scripts.lst", "one.int ; first # local_vars=1\n");
    writeFile(root / "data" / "vault13.gam", "GAME_GLOBAL_VARS:\nGVAR_A :=0;\nGVAR_B :=0;\n");
    writeFile(root / "src" / "one.ssl",
        "procedure map_enter_p_proc begin\n"
        "   set_global_var(2, 1);\n"                     // line 2: vault13.gam has 2 (0-1)
        "   x := global_var(1);\n"                       // fine
        "   add_timer_event(self_obj, 10, 1);\n"         // line 4: no timed_event_p_proc
        "   create_object_sid(0, 100, 0, -1);\n"         // line 5: PID 0
        "   create_object_sid(16777217, 40000, 3, 9);\n" // line 6: no proto, bad hex, elevation, script
        "   move_to(self_obj, 7, 5);\n"                  // line 7: bad elevation
        "end\n");
    cli::LintOptions options;
    options.name = "one";
    options.includeNotes = false;
    int rc = -1;
    const auto j = run(root, [&options](auto& r, auto& out) { return cli::lintScripts(r, options, out); }, rc);
    REQUIRE(rc == 0);
    std::multimap<int, std::string> found; // line -> check
    for (const auto& f : j["findings"]) {
        found.emplace(f["line"].get<int>(), f["check"].get<std::string>());
    }
    const auto at = [&found](int line, const std::string& check) {
        const auto [first, last] = found.equal_range(line);
        return std::count_if(first, last, [&check](const auto& entry) { return entry.second == check; });
    };
    CHECK(at(2, "gvars") == 1);
    CHECK(at(3, "gvars") == 0);
    CHECK(at(4, "timers") == 1);
    CHECK(at(5, "objects") == 1);
    CHECK(at(6, "objects") == 2); // no proto, and script 9 is not a scripts.lst line
    CHECK(at(6, "tiles") == 2);   // hex 40000 and elevation 3
    CHECK(at(7, "tiles") == 1);
    fs::remove_all(root);
}

TEST_CASE("placementFindings checks a script against where the map places it", "[script_lint]") {
    resource::ScriptReport report;
    report.script = "one.ssl";
    report.procedures.push_back({ "critter_p_proc", true, true, 0, { "one.ssl", 3 }, true });
    report.locals.push_back({ ssl::VariableRef{}, { "one.ssl", 5 } });

    const auto checks = [&report](resource::ScriptPlacement placement) {
        std::vector<std::string> out;
        for (const auto& f : resource::placementFindings(report, placement)) {
            out.push_back(f.check);
        }
        return out;
    };
    CHECK(checks(resource::ScriptPlacement::Critter).empty());
    CHECK(checks(resource::ScriptPlacement::Item) == std::vector<std::string>{ "handlers" });
    CHECK(checks(resource::ScriptPlacement::Spatial) == std::vector<std::string>{ "spatial", "handlers" });
    CHECK(checks(resource::ScriptPlacement::Map) == std::vector<std::string>{ "local-vars", "handlers" });
}

namespace {

ssl::IntInstruction op(const char* name, int pops, int pushes) {
    ssl::IntInstruction instruction;
    instruction.name = name;
    instruction.pops = pops;
    instruction.pushes = pushes;
    return instruction;
}

ssl::IntInstruction literal(int32_t value) {
    ssl::IntInstruction push = op("OPCODE_PUSH", 0, 1);
    push.literal = value;
    return push;
}

// A compiled one.int (scripts.lst line 1) with sslc's placeholder procedure first.
ssl::IntProgram compiledOne() {
    ssl::IntProgram program;
    program.procedures.push_back({ "..............", 0, 0, 0, 0, 0, {}, {} });
    ssl::IntProcedure talk{ "talk_p_proc", 0, 0, 0, 0, 0, {}, {} };
    const auto get = op("op_get_local_var", 1, 1);
    const auto set = op("op_set_local_var", 2, 0);
    talk.code = {
        // reads local variable 1
        literal(1),
        get,
        // sets local variable 5 to 1: past the 2 scripts.lst gives
        literal(5),
        literal(1),
        set,
        // adds 1 to local variable 0: reads and sets it
        literal(0),
        literal(0),
        get,
        literal(1),
        op("OPCODE_ADD", 2, 1),
        set,
        // control flow in the value: the index is not known
        literal(3),
        literal(0),
        op("OPCODE_IF", -1, -1),
        set,
        // message 150 of list 1: not in one.msg
        literal(1),
        literal(150),
        op("op_message_str", 2, 1),
        // a reply given as a string, not a message number
        literal(1),
        op("OPCODE_PUSH", 0, 1),
        op("op_gsay_reply", 2, 0),
        // reads global variable 7
        literal(7),
        op("op_get_global_var", 1, 1),
        // a timer on self_obj, in 10 ticks, with parameter 2
        op("op_self_obj", 0, 1),
        literal(10),
        literal(2),
        op("op_add_timer_event", 3, 0),
    };
    program.procedures.push_back(std::move(talk));
    return program;
}

} // namespace

TEST_CASE("reportInt reads a compiled script's procedures and literal variable and message uses", "[script_lint]") {
    const resource::ScriptReport report = resource::reportInt(compiledOne(), "scripts/one.int");
    CHECK(report.fromBytecode);
    REQUIRE(report.procedures.size() == 1); // the placeholder is left out
    CHECK(report.procedures[0].name == "talk_p_proc");
    CHECK(report.procedures[0].entryPoint);

    std::vector<std::pair<bool, std::optional<int>>> locals; // set?, index
    for (const auto& use : report.locals) {
        locals.emplace_back(use.ref.kind == ssl::VariableRef::Kind::Set, use.ref.index);
    }
    CHECK(locals
        == std::vector<std::pair<bool, std::optional<int>>>{ { false, 1 }, { true, 5 }, { false, 0 }, { true, 0 }, { true, std::nullopt } });
    REQUIRE(report.messages.size() == 1);
    CHECK(report.messages[0].ref.list == 1);
    CHECK(report.messages[0].ref.number == 150);
    REQUIRE(report.globals.size() == 1);
    CHECK(report.globals[0].ref.index == 7);
    REQUIRE(report.calls.size() == 1);
    CHECK(report.calls[0].ref.opcode == "add_timer_event");
    CHECK(report.calls[0].ref.self == std::vector<bool>{ true, false, false });
    CHECK(report.calls[0].ref.args[1] == 10);

    // The lint's checks run on it as on a source; the write of unknown index keeps "never read" notes away.
    const fs::path root = makeTree("script_lint_int");
    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    const Lst* lst = resources.repository().load<Lst>("scripts/scripts.lst");
    resource::ScriptLinter linter(resources, lst, "english");
    std::vector<std::string> checks;
    for (const auto& finding : linter.findings(report, 0, true)) {
        checks.push_back(std::format("{} {}:{}", finding.check, finding.where.file, finding.where.line));
    }
    // talk_p_proc also sets a timer on itself, with no timed_event_p_proc to run it.
    CHECK(checks == std::vector<std::string>{ "local-vars scripts/one.int:0", "messages scripts/one.int:0", "timers scripts/one.int:0" });
    // As the map's own script, its local variables read -1.
    CHECK(resource::placementFindings(report, resource::ScriptPlacement::Map).size() == 1);
    fs::remove_all(root);
}

namespace {

// two.int: talk_p_proc does set_local_var(0, 1).
std::vector<uint8_t> compiledTwo() {
    test::ByteWriter code;
    test::pushInt(code, 0);
    test::pushInt(code, 1);
    code.be16(0x80C2); // op_set_local_var
    code.be16(0x8010); // OPCODE_EXIT_PROGRAM
    return test::compiledScript("talk_p_proc", code.data());
}

} // namespace

TEST_CASE("lint_script checks a script's compiled .int when no source is mounted", "[script_lint]") {
    const fs::path root = makeTree("script_lint_compiled");
    const auto bytes = compiledTwo();
    std::ofstream(root / "scripts" / "two.int", std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    cli::LintOptions options;
    options.name = "two";
    int rc = -1;
    const auto one = run(root, [&options](auto& r, auto& out) { return cli::lintScripts(r, options, out); }, rc);
    REQUIRE(rc == 0);
    CHECK(one["fromBytecode"] == true);
    CHECK(one["summary"]["errors"] == 1); // local variable 0, past the 0 scripts.lst gives two
    CHECK(one["findings"][0]["file"] == "scripts/two.int");

    options = {};
    options.all = true;
    options.includeNotes = false;
    const auto all = run(root, [&options](auto& r, auto& out) { return cli::lintScripts(r, options, out); }, rc);
    CHECK(all["scripts"] == 2);
    CHECK(all["fromBytecode"] == 1);
    CHECK(all["unchecked"] == 0);
    fs::remove_all(root);
}

TEST_CASE("reportInt leaves out a branch under a constant condition and folds constant arguments", "[script_lint]") {
    // The original compiler's code for an object created under the condition -1 > 0 (a branch sslc
    // drops), then local variable 1 + 2 set to 7.
    ssl::IntProcedure enter{ "map_enter_p_proc", 0, 0, 0, 0, 0, {}, {} };
    enter.code = { literal(984), literal(1), op("OPCODE_NEGATE", 1, 1), literal(0), op("OPCODE_GREATER_THAN", 2, 1),
        op("OPCODE_IF", -1, -1), literal(1), op("OPCODE_NEGATE", 1, 1), literal(0), literal(0), literal(1),
        op("OPCODE_NEGATE", 1, 1), op("op_create_object", 4, 1), op("OPCODE_POP", -1, -1), literal(1), literal(2),
        op("OPCODE_ADD", 2, 1), literal(7), op("op_set_local_var", 2, 0) };
    for (std::size_t i = 0; i < enter.code.size(); ++i) {
        enter.code[i].offset = 900 + i * 6; // the else address, 984, is the push of 1 in 1 + 2
    }
    ssl::IntProgram program;
    program.procedures.push_back(std::move(enter));
    const resource::ScriptReport report = resource::reportInt(program, "scripts/one.int");
    CHECK(report.calls.empty()); // the create_object_sid never runs
    REQUIRE(report.locals.size() == 1);
    CHECK(report.locals[0].ref.index == 3);
}
