#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <memory>

#include "format/pal/Pal.h"
#include "util/PaletteBlend.h"

using namespace geck;
using namespace geck::palette;

namespace {

// A stand-in for color.pal. Fallout stores 6-bit channels (0-63) and follows them with a table that
// maps every RGB555 value back to the nearest palette index; here the palette is a plain 6x6x6 cube
// so both directions are exact and the expected values can be worked out by hand.
//
// Index 0 is black (the transparent index), and the cube fills 1..216. Slots past that are left
// above 0x3F, which is how a real palette marks an entry it does not map.
constexpr int CUBE_SIDE = 6;
constexpr int CUBE_STEP = 63 / (CUBE_SIDE - 1); // 12 -> channels 0,12,24,36,48,60

std::unique_ptr<Pal> makeCubePal() {
    auto pal = std::make_unique<Pal>("test.pal");

    auto& colors = pal->palette();
    colors.fill(Rgb{ 0xFF, 0xFF, 0xFF }); // unmapped everywhere by default
    colors[0] = Rgb{ 0, 0, 0 };
    for (int r = 0; r < CUBE_SIDE; ++r) {
        for (int g = 0; g < CUBE_SIDE; ++g) {
            for (int b = 0; b < CUBE_SIDE; ++b) {
                const std::size_t index = 1 + static_cast<std::size_t>((r * CUBE_SIDE + g) * CUBE_SIDE + b);
                colors[index] = Rgb{ static_cast<uint8_t>(r * CUBE_STEP),
                    static_cast<uint8_t>(g * CUBE_STEP),
                    static_cast<uint8_t>(b * CUBE_STEP) };
            }
        }
    }

    // The conversion table: for each RGB555 value pick the cube entry with the nearest channels.
    auto& conversion = pal->rgbConversionTable();
    const auto nearest = [](int channel5) { // 0-31 -> the cube step that is closest
        const int channel6 = channel5 * 2;  // the palette's 6-bit scale
        int best = 0;
        int bestDistance = 1000;
        for (int step = 0; step < CUBE_SIDE; ++step) {
            const int distance = std::abs(step * CUBE_STEP - channel6);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = step;
            }
        }
        return best;
    };
    for (int rgb = 0; rgb < static_cast<int>(Pal::NUM_CONVERSION_VALUES); ++rgb) {
        const int r = nearest((rgb & 0x7C00) >> 10);
        const int g = nearest((rgb & 0x3E0) >> 5);
        const int b = nearest(rgb & 0x1F);
        conversion[static_cast<std::size_t>(rgb)]
            = static_cast<uint8_t>(1 + (r * CUBE_SIDE + g) * CUBE_SIDE + b);
    }

    return pal;
}

// The cube index for a colour given as steps along each axis.
constexpr uint8_t cube(int r, int g, int b) {
    return static_cast<uint8_t>(1 + (r * CUBE_SIDE + g) * CUBE_SIDE + b);
}

} // namespace

TEST_CASE("BlendTables mirrors the engine's palette conversions", "[palette]") {
    const auto pal = makeCubePal();
    const BlendTables tables(*pal);

    // Color2RGB halves the palette's 6-bit channels: step 2 is 24, and 24 >> 1 is 12.
    CHECK(tables.toRgb555(cube(2, 0, 0)) == (12 << 10));
    CHECK(tables.toRgb555(cube(0, 5, 0)) == (30 << 5)); // 60 >> 1
    CHECK(tables.toRgb555(0) == 0);

    // Entries the palette does not map read as black and are excluded.
    CHECK(tables.isMapped(cube(1, 1, 1)));
    CHECK_FALSE(tables.isMapped(255));
    CHECK(tables.toRgb555(255) == 0);

    // The display expansion the engine uses is a two-bit shift, not a rescale to 0-255.
    CHECK(tables.toRgb888(cube(5, 0, 0)) == 0xF00000u); // 60 << 2 == 240
    CHECK(tables.toRgb888(0) == 0x000000u);

    // intensityColorTable: 128 is the colour unchanged, 0 is black, 255 is (near) white.
    const uint8_t midGrey = cube(3, 3, 3);
    CHECK(tables.intensity(midGrey, 128) == midGrey);
    CHECK(tables.intensity(midGrey, 0) == cube(0, 0, 0));
    CHECK(tables.intensity(midGrey, 255) == cube(5, 5, 5));
}

TEST_CASE("commonGrayTable weights green and stays within the blend table's rows", "[palette]") {
    const auto pal = makeCubePal();
    const BlendTables tables(*pal);

    // _commonGrayTable is ((b + 3r + 6g) / 10) >> 2 over 5-bit channels, so it never exceeds 7 —
    // which is exactly the number of blend rows the engine builds beyond the identity row.
    for (int index = 0; index < 256; ++index) {
        CHECK(tables.grayLevel(static_cast<uint8_t>(index)) <= 7);
    }

    CHECK(tables.grayLevel(0) == 0); // the transparent index is forced to zero
    // Green weighs six times as much as blue, so full green outranks full blue.
    CHECK(tables.grayLevel(cube(0, 5, 0)) > tables.grayLevel(cube(0, 0, 5)));
    CHECK(tables.grayLevel(cube(0, 5, 0)) > tables.grayLevel(cube(5, 0, 0)));
    CHECK(tables.grayLevel(cube(5, 5, 5)) == 7); // white saturates
}

TEST_CASE("BlendTable interpolates the destination towards the tint", "[palette]") {
    const auto pal = makeCubePal();
    const BlendTables tables(*pal);
    const BlendTable green(tables, GREEN_RGB);

    const uint8_t black = cube(0, 0, 0);
    const uint8_t red = cube(5, 0, 0);

    // Row 0 is the identity: a source pixel with no grey level leaves the destination alone.
    for (int dest = 0; dest < 256; ++dest) {
        CHECK(green.lookup(0, static_cast<uint8_t>(dest)) == dest);
    }

    // Row 7 is the tint on its own, whatever was underneath.
    CHECK(green.lookup(7, black) == green.lookup(7, red));
    CHECK(green.lookup(7, black) == cube(0, 5, 0)); // the palette's green

    // The rows in between walk from the destination to the tint, so green rises monotonically.
    int previousGreen = -1;
    for (uint8_t row = 0; row <= 7; ++row) {
        const int greenChannel = (tables.toRgb555(green.lookup(row, black)) & 0x3E0) >> 5;
        CHECK(greenChannel >= previousGreen);
        previousGreen = greenChannel;
    }
}

TEST_CASE("blendPixel reproduces the worldmap circle's translucent tint", "[palette]") {
    const auto pal = makeCubePal();
    const BlendTables tables(*pal);
    const BlendTable green(tables, GREEN_RGB);

    const uint8_t terrain = cube(3, 2, 1); // some brown ground under the circle

    // This is the whole of _dark_translucent_trans_buf_to_buf at intensity 0x10000: grey level picks
    // the row, the row blends against the destination, the intensity table passes it through.
    for (int src = 1; src < 256; ++src) {
        const uint8_t source = static_cast<uint8_t>(src);
        const uint8_t expected = tables.intensity(green.lookup(tables.grayLevel(source), terrain), 128);
        CHECK(green.blendPixel(source, terrain) == expected);
    }

    // A dark source barely touches the terrain; a bright one replaces it with green. That contrast
    // is what makes the circle read as a bright ring around a tinted interior.
    CHECK(green.blendPixel(cube(0, 0, 1), terrain) == terrain);
    CHECK(green.blendPixel(cube(5, 5, 5), terrain) == cube(0, 5, 0));
}
