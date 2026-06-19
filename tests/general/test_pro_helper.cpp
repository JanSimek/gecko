#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

namespace fs = std::filesystem;
using geck::ProHelper;
using geck::resource::GameResources;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

// OBJECT_TYPE::ITEM == 0, so an item PID is just its (1-based) LST index.
uint32_t itemPid(uint32_t index) {
    return (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::ITEM) << 24) | index;
}

} // namespace

TEST_CASE("ProHelper::basePath resolves a 1-based PID index to its LST entry", "[prohelper][pro]") {
    const fs::path root = fs::path(GECK_TEST_TMP_DIR) / "geck_prohelper_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "proto/items/items.lst", "knife.pro\nspear.pro\n");

    GameResources resources;
    resources.files().addDataPath(root.string());

    // The PID index is 1-based: index 1 maps to the first LST line.
    CHECK(ProHelper::basePath(resources, itemPid(1)) == "proto/items/knife.pro");
    CHECK(ProHelper::basePath(resources, itemPid(2)) == "proto/items/spear.pro");

    fs::remove_all(root, ec);
}

TEST_CASE("ProHelper::basePath rejects out-of-range PID indices", "[prohelper][pro]") {
    const fs::path root = fs::path(GECK_TEST_TMP_DIR) / "geck_prohelper_test_oob";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "proto/items/items.lst", "knife.pro\n");

    GameResources resources;
    resources.files().addDataPath(root.string());

    // Index 0 is invalid (PIDs are 1-based) and must be rejected rather than
    // underflowing the index - 1 LST lookup into an out-of-range access.
    CHECK_THROWS(ProHelper::basePath(resources, itemPid(0)));
    // An index past the end of the LST is rejected too.
    CHECK_THROWS(ProHelper::basePath(resources, itemPid(2)));

    fs::remove_all(root, ec);
}
