#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "cli/GvarRefs.h"
#include "resource/GameResources.h"

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

nlohmann::json runFindGvar(const fs::path& root, const std::string& gvar, int& rc) {
    resource::GameResources resources;
    resources.files().addDataPath(root.string());
    std::ostringstream oss;
    rc = cli::findGvarRefs(resources, gvar, oss);
    return nlohmann::json::parse(oss.str());
}

} // namespace

TEST_CASE("findGvarRefs classifies set/get, honors word boundaries, scans headers, ignores comments", "[gvar_refs]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "gvar_refs_src";
    fs::remove_all(root);
    writeFile(root / "a.ssl",
        "if (global_var(GVAR_FOO) == 1) then begin\n" // get
        "   set_global_var(GVAR_FOO, 2);\n"           // set
        "end\n"
        "// set_global_var(GVAR_FOO, 9);  commented out\n" // line comment -> ignored
        "x := global_var(GVAR_FOOBAR);\n");                // must NOT match GVAR_FOO
    writeFile(root / "headers" / "caravan.h",
        "#define get_foo   global_var(GVAR_FOO)\n"); // gvar reached only via a header macro -> a get

    int rc = -1;
    const auto j = runFindGvar(root, "GVAR_FOO", rc);

    CHECK(rc == 0);
    CHECK(j["stats"]["refs"] == 3);    // a.ssl get + set (comment excluded) + caravan.h macro
    CHECK(j["stats"]["setters"] == 1); // only the set_global_var line
    CHECK(j["stats"]["scripts"] == 2); // a.ssl + caravan.h
    bool sawFoobar = false;
    for (const auto& ref : j["refs"]) {
        if (ref["text"].get<std::string>().find("GVAR_FOOBAR") != std::string::npos) {
            sawFoobar = true;
        }
    }
    CHECK_FALSE(sawFoobar); // whole-word match: GVAR_FOO must not match GVAR_FOOBAR
    fs::remove_all(root);
}

TEST_CASE("findGvarRefs: unused-with-sources is success, no-sources is an error", "[gvar_refs]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "gvar_refs_empty";
    fs::remove_all(root);
    writeFile(root / "a.ssl", "set_global_var(GVAR_OTHER, 1);\n");

    int rc = -1;
    const auto j = runFindGvar(root, "GVAR_NEVER_USED", rc);
    CHECK(rc == 0); // scanned a source, found nothing -> a valid "unused" answer, not an error
    CHECK(j["stats"]["refs"] == 0);
    fs::remove_all(root);

    // Nothing mounted at all -> error, so the MCP flags isError (vs. a genuine "unused").
    resource::GameResources empty;
    std::ostringstream oss;
    CHECK(cli::findGvarRefs(empty, "GVAR_FOO", oss) != 0);
}
