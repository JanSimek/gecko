#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

#include "format/gam/Gam.h"
#include "reader/gam/GamReader.h"
#include "support/Fixtures.h"

namespace {
std::vector<uint8_t> gamBytes(std::string_view s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}
} // namespace

TEST_CASE("Parse .gam file", "[gam]") {
    geck::GamReader gam_reader{};
    auto gam_file = gam_reader.openFile(geck::test::dataPath("test.gam"));

    constexpr int vars_count = 10;

    for (int index = 0; index < vars_count; index++) {
        REQUIRE(gam_file->mvarValue(index) == index + 1);
        REQUIRE(gam_file->gvarValue(index) == vars_count - index);
    }
}

TEST_CASE("GamReader parses gvars and mvars, skipping comments and blanks", "[gam]") {
    geck::GamReader reader;
    auto gam = reader.openFile("inline.gam", gamBytes("GAME_GLOBAL_VARS:\n"
                                                      "// a comment\n"
                                                      "GVAR_FOO := 5 ;\n"
                                                      "\n"
                                                      "GVAR_NEG := -1 ;\n" // negative default must be read, not skipped
                                                      "GVAR_BAR := 10 ;\n"
                                                      "MAP_GLOBAL_VARS:\n"
                                                      "MVAR_BAZ := 7 ;\n"));

    REQUIRE(gam != nullptr);
    CHECK(gam->gvarValue("GVAR_FOO") == 5);
    CHECK(gam->gvarValue("GVAR_NEG") == -1);
    CHECK(gam->gvarValue("GVAR_BAR") == 10);
    CHECK(gam->mvarValue("MVAR_BAZ") == 7);
    // Variables keep their file order — a negative-default gvar must not be dropped, or every later
    // gvar's ordinal shifts (gvar ordinals are how quests.txt / scripts reference them).
    CHECK(gam->gvarCount() == 3);
    CHECK(gam->gvarKey(0) == "GVAR_FOO");
    CHECK(gam->gvarKey(1) == "GVAR_NEG");
    CHECK(gam->gvarValue(2) == 10); // GVAR_BAR keeps ordinal 2, not shifted to 1
    CHECK(gam->mvarValue(0) == 7);
}

TEST_CASE("GamReader rejects an empty file", "[gam]") {
    geck::GamReader reader;
    REQUIRE_THROWS(reader.openFile("empty.gam", std::vector<uint8_t>{}));
}

TEST_CASE("GamReader rejects a file with no GVARS/MVARS sections", "[gam]") {
    geck::GamReader reader;
    REQUIRE_THROWS(reader.openFile("bad.gam", gamBytes("SOME_KEY := 1 ;\n")));
}

TEST_CASE("GamReader rejects a variable declared outside any section", "[gam]") {
    geck::GamReader reader;
    // The section marker is present, but a variable precedes it.
    REQUIRE_THROWS(reader.openFile("bad.gam", gamBytes("GVAR_X := 1 ;\nGAME_GLOBAL_VARS:\n")));
}
