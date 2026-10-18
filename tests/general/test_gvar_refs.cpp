#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <ostream>
#include <string>

#include "cli/GvarRefs.h"
#include "resource/GameResources.h"

#include "support/CliJson.h"
#include "support/TempFile.h"

using namespace geck;
namespace fs = std::filesystem;
using geck::test::writeFile;

namespace {

nlohmann::json runFindGvar(const fs::path& root, const std::string& gvar) {
    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    return test::runJson([&](std::ostream& out) { return cli::findGvarRefs(resources, gvar, out); });
}

} // namespace

TEST_CASE("findGvarRefs reports the reads and writes compiled code makes, through macro aliases", "[gvar_refs]") {
    const test::TempDir rootDir{ "gvar_refs_src" };
    const fs::path& root = rootDir.path();
    writeFile(root / "headers" / "global.h", "#define GVAR_FOO (7)\n#define GVAR_FOOBAR (8)\n");
    // A gvar reached only through a header macro alias, as caravan.h's are.
    writeFile(root / "headers" / "caravan.h", "#define get_foo   global_var(GVAR_FOO)\n#define unused_set set_global_var(GVAR_FOO, 5)\n");
    writeFile(root / "arroyo" / "a.ssl",
        "#include \"../headers/global.h\"\n"
        "#include \"../headers/caravan.h\"\n"
        "procedure map_enter_p_proc begin\n"
        "   if (global_var(GVAR_FOO) == 1) then begin\n" // get, line 4
        "      set_global_var(GVAR_FOO, 2);\n"           // set, line 5
        "   end\n"
        "   // set_global_var(GVAR_FOO, 9);  commented out\n"
        "   x := global_var(GVAR_FOOBAR);\n" // another gvar
        "   y := get_foo;\n"                 // get through the alias, line 9
        "end\n"
        "procedure never_called begin\n"
        "   set_global_var(GVAR_FOO, 3);\n" // not compiled: nothing calls it
        "end\n");

    const auto j = runFindGvar(root, "GVAR_FOO");

    REQUIRE(j["stats"]["refs"] == 3);
    CHECK(j["stats"]["setters"] == 1);
    CHECK(j["stats"]["scripts"] == 1);
    const auto& refs = j["refs"];
    CHECK(refs[0]["script"] == "a.ssl");
    CHECK(refs[0]["procedure"] == "map_enter_p_proc");
    CHECK(refs[0]["line"] == 4);
    CHECK(refs[0]["kind"] == "get");
    CHECK(refs[1]["line"] == 5);
    CHECK(refs[1]["kind"] == "set");
    CHECK(refs[1]["text"] == "set_global_var(GVAR_FOO, 2);");
    CHECK(refs[2]["line"] == 9);
    CHECK(refs[2]["via"] == "get_foo");
}

TEST_CASE("findGvarRefs: unused-with-sources is success, no-sources is an error", "[gvar_refs]") {
    const test::TempDir rootDir{ "gvar_refs_empty" };
    const fs::path& root = rootDir.path();
    writeFile(root / "a.ssl", "procedure start begin\n   set_global_var(GVAR_OTHER, 1);\nend\n");

    const auto j = runFindGvar(root, "GVAR_NEVER_USED");
    // Read a source and found nothing: a valid "unused" answer (runFindGvar requires success), not an error.
    CHECK(j["stats"]["refs"] == 0);

    // Nothing mounted at all -> error, so the MCP flags isError (vs. a genuine "unused").
    resource::GameResources empty;
    std::ostringstream oss;
    CHECK(cli::findGvarRefs(empty, "GVAR_FOO", oss) != 0);
}

TEST_CASE("findGvarRefs matches a gvar whose header is missing by its name", "[gvar_refs]") {
    const test::TempDir rootDir{ "gvar_refs_nohdr" };
    const fs::path& root = rootDir.path();
    writeFile(root / "a.ssl", "procedure start begin\n   set_global_var(GVAR_FOO, 1);\n   x := global_var(GVAR_FOOBAR);\nend\n");

    const auto j = runFindGvar(root, "GVAR_FOO");
    REQUIRE(j["stats"]["refs"] == 1); // GVAR_FOOBAR is a different name
    CHECK(j["refs"][0]["kind"] == "set");
}
