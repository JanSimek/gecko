#include <catch2/catch_test_macros.hpp>

#include "resource/GameResources.h"
#include "resource/ScriptNames.h"

#include <filesystem>
#include <fstream>

using namespace geck;

namespace fs = std::filesystem;

namespace {
void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}
} // namespace

// scrname.msg names scripts at id (0-based scripts.lst index + 101). resource::scriptDisplayName must
// apply exactly that offset, and degrade to "" when the file or entry is absent (callers then show the
// bare .lst name).
TEST_CASE("scriptDisplayName resolves scrname.msg[programIndex + 101]", "[scriptnames]") {
    const fs::path root = fs::path(GECK_TEST_TMP_DIR) / "scriptnames_test";
    fs::remove_all(root);
    writeFile(root / "text/english/game/scrname.msg",
        "{101}{}{Combat AI}\n"
        "{102}{}{Door}\n");

    resource::GameResources resources;
    resources.files().addDataPath(root.string());

    CHECK(resource::scriptDisplayName(resources, 0) == "Combat AI"); // index 0 -> id 101
    CHECK(resource::scriptDisplayName(resources, 1) == "Door");      // index 1 -> id 102
    CHECK(resource::scriptDisplayName(resources, -1).empty());       // no negative index
    CHECK(resource::scriptDisplayName(resources, 50).empty());       // no id 151 in the file

    fs::remove_all(root);
}

TEST_CASE("scriptDisplayName is empty when scrname.msg isn't mounted", "[scriptnames]") {
    resource::GameResources resources; // nothing mounted
    CHECK(resource::scriptDisplayName(resources, 0).empty());
}
