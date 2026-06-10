#include "Pal.h"

namespace geck {

std::array<Rgb, Pal::NUM_PALETTE_COLORS>& Pal::palette() {
    return _palette;
}

const std::array<Rgb, Pal::NUM_PALETTE_COLORS>& Pal::palette() const {
    return _palette;
}

std::array<uint8_t, Pal::NUM_CONVERSION_VALUES>& Pal::rgbConversionTable() {
    return _rgbConversionTable;
}

void Pal::setRgbConversionTable(const std::array<uint8_t, NUM_CONVERSION_VALUES>& rgbConversionTable) {
    _rgbConversionTable = rgbConversionTable;
}

void Pal::setPalette(const std::array<Rgb, NUM_PALETTE_COLORS>& value) {
    _palette = value;
}

} // namespace geck
