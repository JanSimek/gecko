#include "util/PaletteBlend.h"

#include "format/pal/Pal.h"

namespace geck::palette {

namespace {

    // The engine keeps colours as RGB555 while blending; these split one out into channels.
    constexpr int redOf(int rgb) { return (rgb & 0x7C00) >> 10; }
    constexpr int greenOf(int rgb) { return (rgb & 0x3E0) >> 5; }
    constexpr int blueOf(int rgb) { return rgb & 0x1F; }

    constexpr int packRgb555(int r, int g, int b) { return (r << 10) | (g << 5) | b; }

    // The palette's 6-bit channels are the raw file bytes; anything above 0x3F is not a colour but
    // a marker for an unmapped slot. colorPaletteLoad zeroes those, so Color2RGB reads black.
    constexpr std::uint8_t MAX_CHANNEL = 0x3F;

} // namespace

BlendTables::BlendTables(const Pal& pal)
    : _pal(pal.rgbConversionTable()) {
    // colorPaletteLoad: mark the slots the palette actually maps and zero the rest, then Color2RGB
    // (6-bit channels halved to 5) for every index.
    const auto& colors = pal.palette();
    for (std::size_t index = 0; index < COLORS; ++index) {
        const Rgb& color = colors[index];
        const bool mapped = color.r <= MAX_CHANNEL && color.g <= MAX_CHANNEL && color.b <= MAX_CHANNEL;
        _mapped[index] = mapped ? 1 : 0;

        const int r = mapped ? color.r : 0;
        const int g = mapped ? color.g : 0;
        const int b = mapped ? color.b : 0;
        _rgb555[index] = packRgb555(r >> 1, g >> 1, b >> 1);
        // The renderer shows the stored 6-bit channels shifted up by two (svga.cc).
        _rgb888[index] = static_cast<std::uint32_t>((r << 2) << 16 | (g << 2) << 8 | (b << 2));
    }

    // _obj_blend_table_init: the grey level that selects a blend row, weighted towards green.
    // r/g/b are 0-31 here, so the result never exceeds 7.
    for (std::size_t index = 0; index < COLORS; ++index) {
        const int rgb = _rgb555[index];
        _commonGray[index] = static_cast<std::uint8_t>(((blueOf(rgb) + 3 * redOf(rgb) + 6 * greenOf(rgb)) / 10) >> 2);
    }
    _commonGray[0] = 0;

    // _setIntensityTables / _setIntensityTableColor. Levels 0-127 darken towards black and 128-255
    // lighten towards white, in 1/128 steps; level 128 is the colour unchanged. Unmapped colours
    // get an all-zero row, matching the engine's memset.
    for (std::size_t color = 0; color < COLORS; ++color) {
        if (_mapped[color] == 0) {
            continue; // _intensity is value-initialised to zero
        }

        const int rgb = _rgb555[color];
        const int r = redOf(rgb);
        const int g = greenOf(rgb);
        const int b = blueOf(rgb);

        int shift = 0;
        for (std::size_t level = 0; level < 128; ++level) {
            const int darker = packRgb555((r * shift) >> 16, (g * shift) >> 16, (b * shift) >> 16);
            _intensity[color * COLORS + level] = _pal[static_cast<std::size_t>(darker)];

            const int lighter = packRgb555(r + (((0x1F - r) * shift) >> 16),
                g + (((0x1F - g) * shift) >> 16),
                b + (((0x1F - b) * shift) >> 16));
            _intensity[color * COLORS + 128 + level] = _pal[static_cast<std::size_t>(lighter)];

            shift += 512;
        }
    }
}

BlendTable::BlendTable(const BlendTables& tables, std::uint32_t tintRgb)
    : _tables(&tables) {
    // The engine's COLOR_* macros are _colorTable lookups, so the tint is snapped to the palette
    // before _buildBlendTable reads its channels back out.
    const std::uint8_t tint = tables.fromRgb555(rgb555(tintRgb));
    const int tintColor = tables.toRgb555(tint);
    const int r = redOf(tintColor);
    const int g = greenOf(tintColor);
    const int b = blueOf(tintColor);

    // Row 0 is the identity: a grey level of 0 leaves the destination untouched.
    for (std::size_t index = 0; index < 256; ++index) {
        _rows[index] = static_cast<std::uint8_t>(index);
    }

    // Rows 1-7 mix the destination with the tint at a falling destination weight, so row 7 is the
    // tint on its own. (_buildBlendTable then writes six flat shading rows past these; nothing
    // reads them, because _commonGrayTable tops out at 7.)
    int accumulatedR = r;
    int accumulatedG = g;
    int accumulatedB = b;
    int destWeight = 6;
    for (std::size_t row = 1; row < ROWS; ++row) {
        for (std::size_t dest = 0; dest < 256; ++dest) {
            const int destColor = tables.toRgb555(static_cast<std::uint8_t>(dest));
            const int mixed = packRgb555((accumulatedR + redOf(destColor) * destWeight) / 7,
                (accumulatedG + greenOf(destColor) * destWeight) / 7,
                (accumulatedB + blueOf(destColor) * destWeight) / 7);
            _rows[row * 256 + dest] = tables.fromRgb555(mixed);
        }

        --destWeight;
        accumulatedR += r;
        accumulatedG += g;
        accumulatedB += b;
    }
}

std::uint8_t BlendTable::blendPixel(std::uint8_t src, std::uint8_t dest) const {
    const std::uint8_t blended = lookup(_tables->grayLevel(src), dest);
    return _tables->intensity(blended, FULL_INTENSITY / 512);
}

} // namespace geck::palette
