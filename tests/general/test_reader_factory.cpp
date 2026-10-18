#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "reader/ReaderFactory.h"
#include "format/pro/Pro.h"
#include "format/frm/Frm.h"
#include "format/gam/Gam.h"
#include "format/msg/Msg.h"
#include "format/lst/Lst.h"
#include "format/pal/Pal.h"
#include "support/Fixtures.h"
#include "support/TempFile.h"

TEST_CASE("ReaderFactory picks the reader by file extension", "[reader_factory]") {
    using namespace geck;

    CHECK(ReaderFactory::detectByExtension("test.pro") == ReaderFactory::Format::PRO);
    CHECK(ReaderFactory::detectByExtension("test.frm") == ReaderFactory::Format::FRM);
    CHECK(ReaderFactory::detectByExtension("test.pal") == ReaderFactory::Format::PAL);
    CHECK(ReaderFactory::detectByExtension("test.gam") == ReaderFactory::Format::GAM);
    CHECK(ReaderFactory::detectByExtension("test.msg") == ReaderFactory::Format::MSG);
    CHECK(ReaderFactory::detectByExtension("test.lst") == ReaderFactory::Format::LST);

    // Case insensitive
    CHECK(ReaderFactory::detectByExtension("TEST.PRO") == ReaderFactory::Format::PRO);

    // Split critter animations keep one direction per file
    for (const char* name : { "hmjmpsaa.fr0", "hmjmpsaa.fr1", "hmjmpsaa.fr2", "hmjmpsaa.fr3", "hmjmpsaa.fr4", "hmjmpsaa.fr5" }) {
        CHECK(ReaderFactory::detectByExtension(name) == ReaderFactory::Format::FRM);
    }

    CHECK(ReaderFactory::detectByExtension("test.unknown") == ReaderFactory::Format::UNKNOWN);
    CHECK(ReaderFactory::detectByExtension("test") == ReaderFactory::Format::UNKNOWN);
}

TEST_CASE("ReaderFactory reads a directional .fr0 FRM from memory", "[reader_factory]") {
    using namespace geck;

    const std::vector<uint8_t> data = test::readAllBytes(test::dataPath("flower.frm"));

    const auto frm = ReaderFactory::readFileFromMemory<Frm>(data, "art/critters/flower.fr0");
    REQUIRE(frm != nullptr);
    CHECK(frm->version() == 4);
}

TEST_CASE("ReaderFactory rejects a file whose extension names another or no format", "[reader_factory]") {
    using namespace geck;

    const std::vector<uint8_t> data(64, 0);
    CHECK_THROWS_AS(ReaderFactory::readFileFromMemory<Pro>(data, "art/items/flower.frm"), IOException);
    CHECK_THROWS_AS(ReaderFactory::readFileFromMemory<Lst>(data, "scripts/scripts"), IOException);

    CHECK_THROWS_AS(ReaderFactory::createReader<Pro>(ReaderFactory::Format::FRM), UnsupportedFormatException);
}
