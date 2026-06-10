#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "format/pal/Pal.h"
#include "reader/pal/PalReader.h"
#include "support/ByteWriter.h"
#include "support/Fixtures.h"

using namespace geck;

TEST_CASE("PalReader parses the real color.pal palette and conversion table", "[pal]") {
    PalReader reader;
    auto pal = reader.openFile(geck::test::dataPath("color.pal"));

    REQUIRE(pal != nullptr);
    REQUIRE(pal->palette().size() == Pal::NUM_PALETTE_COLORS);
    REQUIRE(pal->rgbConversionTable().size() == Pal::NUM_CONVERSION_VALUES);

    // Index 0 is the engine's transparent marker (255,255,255); Frame::rgba keys on it.
    CHECK(pal->palette()[0].r == 255);
    CHECK(pal->palette()[0].g == 255);
    CHECK(pal->palette()[0].b == 255);

    // Decoded straight from the fixture bytes (palette index 1 = 0x3B,0x3B,0x3B).
    CHECK(pal->palette()[1].r == 59);
    CHECK(pal->palette()[1].g == 59);
    CHECK(pal->palette()[1].b == 59);

    CHECK(pal->rgbConversionTable()[0] == 0xE4);
}

TEST_CASE("PalReader maps raw bytes onto palette entries", "[pal]") {
    geck::test::ByteWriter w;
    for (int i = 0; i < 256; ++i) {
        if (i == 1) {
            w.u8(10).u8(20).u8(30);
        } else if (i == 255) {
            w.u8(63).u8(62).u8(61);
        } else {
            w.u8(0).u8(0).u8(0);
        }
    }
    w.u8(7);              // conversion table entry 0
    w.fill(0, 32768 - 1); // remaining conversion table
    REQUIRE(w.size() == 0x8300);

    PalReader reader;
    auto pal = reader.openFile("synthetic.pal", w.data());

    REQUIRE(pal != nullptr);
    CHECK(pal->palette()[1].r == 10);
    CHECK(pal->palette()[1].g == 20);
    CHECK(pal->palette()[1].b == 30);
    CHECK(pal->palette()[255].r == 63);
    CHECK(pal->palette()[255].g == 62);
    CHECK(pal->palette()[255].b == 61);
    CHECK(pal->rgbConversionTable()[0] == 7);
}

TEST_CASE("PalReader rejects a file smaller than a full palette", "[pal]") {
    PalReader reader;
    REQUIRE_THROWS(reader.openFile("short.pal", std::vector<uint8_t>(100, 0)));
}
