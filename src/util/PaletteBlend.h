#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace geck {
class Pal;
}

/// @file
/// @brief Fallout 2's palette-index colour maths, ported from fallout2-ce.
///
/// The engine never blends in RGB. Every pixel it draws is an index into `color.pal`, and every
/// effect (translucency, tinting, dimming) is a lookup through tables derived from that palette.
/// Reproducing an engine-drawn overlay pixel-for-pixel therefore means doing the same lookups on
/// the same indices, then converting to RGB only at the very end.
///
/// @see fallout2-ce `color.cc` (`_buildBlendTable`, `_setIntensityTableColor`, `Color2RGB`)
/// @see fallout2-ce `object.cc` (`_obj_blend_table_init`, `_dark_translucent_trans_buf_to_buf`)

namespace geck::palette {

/// A 24-bit 0xRRGGBB colour reduced to the 15-bit RGB555 index `_colorTable` is keyed by.
/// Mirrors fallout2-ce color.h `rgb555`.
constexpr int rgb555(std::uint32_t rgb) {
    return static_cast<int>(((rgb >> 19) & 0x1F) << 10 | ((rgb >> 11) & 0x1F) << 5 | ((rgb >> 3) & 0x1F));
}

/// The tint the worldmap's city circles are blended with — fallout2-ce worldmap.cc uses
/// `_getColorBlendTable(COLOR_GREEN)`, and `COLOR_GREEN` is `_colorTable[rgb555(0x00FF00)]`.
constexpr std::uint32_t GREEN_RGB = 0x00FF00;

/// The engine's derived colour tables for one loaded palette.
///
/// Built once per `color.pal` and then only read, so a renderer can blend as many overlays as it
/// likes without recomputing anything. All indices are palette indices (0-255) unless stated.
class BlendTables {
public:
    /// Derives every table from @p pal. The palette's own RGB-to-index conversion table (the
    /// 32,768-entry block `color.pal` stores after its 768 RGB bytes) is used verbatim as the
    /// engine's `_colorTable`.
    explicit BlendTables(const Pal& pal);

    /// The palette entry @p index as RGB555. Mirrors `Color2RGB`: the palette's 6-bit channels are
    /// halved to 5 bits. Colours the palette does not map read as 0.
    [[nodiscard]] int toRgb555(std::uint8_t index) const { return _rgb555[index]; }

    /// The nearest palette index to an RGB555 value (`_colorTable`).
    [[nodiscard]] std::uint8_t fromRgb555(int rgb) const {
        return _pal[static_cast<std::size_t>(rgb) & (RGB555_VALUES - 1)];
    }

    /// The "common" grey level (0-7) of a palette index, weighted towards green. This is the row
    /// selector into a blend table: 0 leaves the destination alone, 7 replaces it with the tint.
    /// Mirrors `_commonGrayTable` (`_obj_blend_table_init`).
    [[nodiscard]] std::uint8_t grayLevel(std::uint8_t index) const { return _commonGray[index]; }

    /// `intensityColorTable[color][intensity]`: @p color darkened (intensity < 128) or lightened
    /// (intensity > 128) in 1/128 steps, resolved back to a palette index. 128 is unchanged.
    [[nodiscard]] std::uint8_t intensity(std::uint8_t color, std::uint8_t level) const {
        return _intensity[index2d(color, level)];
    }

    /// The engine's RGB for a palette index, expanded from the stored 6-bit channels the way the
    /// renderer displays them (`x << 2`, as in fallout2-ce svga.cc `directDrawSetPalette` and in
    /// this project's Frame::rgba). Returns the three channels packed as 0xRRGGBB.
    [[nodiscard]] std::uint32_t toRgb888(std::uint8_t index) const { return _rgb888[index]; }

    /// Whether the palette maps this index at all. Unmapped entries are forced to black and are
    /// excluded from the intensity table, exactly as `colorPaletteLoad` does.
    [[nodiscard]] bool isMapped(std::uint8_t index) const { return _mapped[index] != 0; }

private:
    static constexpr std::size_t COLORS = 256;
    static constexpr std::size_t RGB555_VALUES = 32768;

    [[nodiscard]] static constexpr std::size_t index2d(std::uint8_t row, std::uint8_t column) {
        return static_cast<std::size_t>(row) * COLORS + column;
    }

    std::array<std::uint8_t, RGB555_VALUES> _pal{};
    std::array<int, COLORS> _rgb555{};
    std::array<std::uint32_t, COLORS> _rgb888{};
    std::array<std::uint8_t, COLORS> _mapped{};
    std::array<std::uint8_t, COLORS> _commonGray{};
    std::array<std::uint8_t, COLORS * COLORS> _intensity{};
};

/// One tint's blend table — the engine's `_getColorBlendTable(colour)` result.
///
/// Laid out as 8 rows of 256 entries. Row `g` (a @ref BlendTables::grayLevel) holds, for every
/// destination index, the palette index that is `g/7` of the way from that destination towards the
/// tint. Row 0 is the identity row. (The engine allocates six further rows of flat shading beyond
/// these; nothing indexes them, because `_commonGrayTable` never exceeds 7.)
class BlendTable {
public:
    /// Builds the table for @p tintRgb (a 0xRRGGBB colour, snapped to the nearest palette entry
    /// first, as the engine's `COLOR_*` macros do). Mirrors `_buildBlendTable`. @p tables must
    /// outlive the table, which keeps referring to it for @ref blendPixel.
    BlendTable(const BlendTables& tables, std::uint32_t tintRgb);

    /// The blend of destination index @p dest at grey level @p gray (0-7).
    [[nodiscard]] std::uint8_t lookup(std::uint8_t gray, std::uint8_t dest) const {
        return _rows[static_cast<std::size_t>(gray) * 256 + dest];
    }

    /// Blends one source pixel over one destination pixel the way
    /// `_dark_translucent_trans_buf_to_buf` does at full intensity (0x10000): the source index
    /// picks a grey level, that selects the blend row, and the result is passed through the
    /// intensity table. Source index 0 is transparent and must be skipped by the caller.
    [[nodiscard]] std::uint8_t blendPixel(std::uint8_t src, std::uint8_t dest) const;

    /// The `intensity` argument `_dark_translucent_trans_buf_to_buf` is called with for the
    /// worldmap circles (fallout2-ce worldmap.cc), i.e. "unchanged".
    static constexpr int FULL_INTENSITY = 0x10000;

private:
    static constexpr std::size_t ROWS = 8;

    // A pointer rather than a reference: a blend table is a value a renderer may well want to keep
    // several of (one per tint) and reassign as the palette changes, and a reference member would
    // make it unassignable for no gain.
    const BlendTables* _tables;
    std::array<std::uint8_t, ROWS * 256> _rows{};
};

} // namespace geck::palette
