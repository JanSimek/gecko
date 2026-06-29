#include <catch2/catch_test_macros.hpp>

#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"
#include "util/FileIo.h"

#include <filesystem>

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
namespace fs = std::filesystem;
using geck::io::writeFile;

// MapNameResolver is the single home for the engine's map.msg index formulas. These mirror
// fallout2-ce: per-map names at map.msg[index*3 + elevation + 200] (map.cc mapGetName) and worldmap
// area/city labels at map.msg[1500 + areaIndex] (worldmap.cc wmGetAreaIdxName). Built on a tiny
// mounted maps.txt + map.msg fixture so the index math is exercised, not just the no-data fallback.
TEST_CASE("MapNameResolver resolves map and area names from map.msg", "[mapnameresolver]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "mapnameresolver";
    fs::remove_all(base);
    writeFile(base / "data" / "maps.txt",
        "[Map 0]\nlookup_name=Arroyo Bridge\nmap_name=artemple\n"
        "[Map 1]\nlookup_name=Arroyo Village\nmap_name=arvillage");
    // map.msg: per-map names at index*3+elev+200; area/city labels at 1500+areaIndex.
    writeFile(base / "text" / "english" / "game" / "map.msg",
        "{200}{}{Temple of Trials}\n{201}{}{Temple Level 1}\n{203}{}{Arroyo Village}\n"
        "{1500}{}{Arroyo}\n{1501}{}{Klamath}");

    resource::GameResources resources;
    resources.files().addDataPath(base.string());
    const resource::MapNameResolver names(resources);

    SECTION("displayName uses map.msg[index*3 + elevation + 200]") {
        CHECK(names.displayName(0, 0) == "Temple of Trials");
        CHECK(names.displayName(0, 1) == "Temple Level 1");
        CHECK(names.displayName(1, 0) == "Arroyo Village");
    }
    SECTION("areaName uses map.msg[1500 + areaIndex]") {
        CHECK(names.areaName(0) == "Arroyo");
        CHECK(names.areaName(1) == "Klamath");
    }
    SECTION("negative or unmapped indices resolve to empty, never throw") {
        CHECK(names.areaName(-1).empty());
        CHECK(names.displayName(-1, 0).empty());
        CHECK(names.areaName(99).empty()); // no {1599} entry in the fixture
    }
    fs::remove_all(base);
}
