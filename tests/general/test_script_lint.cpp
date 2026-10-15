#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
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
    CHECK(j["summary"]["notes"] == 1);

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

TEST_CASE("lint_script all=true checks every script with a source and leaves notes out", "[script_lint]") {
    const fs::path root = makeTree("script_lint_all");
    cli::LintOptions options;
    options.all = true;
    options.includeNotes = false;
    int rc = -1;
    const auto j = run(root, [&options](auto& r, auto& out) { return cli::lintScripts(r, options, out); }, rc);
    REQUIRE(rc == 0);
    CHECK(j["scripts"] == 1); // two.int has no source
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
    CHECK(analysis["findings"].size() == 4);
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
