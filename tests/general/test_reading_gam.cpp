#include <catch2/catch_test_macros.hpp>

#include "format/gam/Gam.h"
#include "reader/gam/GamReader.h"

TEST_CASE("Parse .gam file", "[gam]") {
    geck::GamReader gam_reader{};
    auto gam_file = gam_reader.openFile("data/test.gam");

    constexpr int vars_count = 10;

    for (int index = 0; index < vars_count; index++) {
        REQUIRE(gam_file->mvarValue(index) == index + 1);
        REQUIRE(gam_file->gvarValue(index) == vars_count - index);
    }
}
