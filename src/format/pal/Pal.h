#pragma once

#include <array>
#include <fstream>

#include "format/IFile.h"

namespace geck {

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class Pal : public IFile {
public:
    Pal(const std::filesystem::path path)
        : IFile(path) { }

    static constexpr unsigned NUM_PALETTE_COLORS = 256;
    static constexpr unsigned NUM_CONVERSION_VALUES = 32768;

    std::array<Rgb, NUM_PALETTE_COLORS>& palette();
    void setPalette(const std::array<Rgb, NUM_PALETTE_COLORS>& value);

    std::array<uint8_t, NUM_CONVERSION_VALUES>& rgbConversionTable();
    void setRgbConversionTable(const std::array<uint8_t, NUM_CONVERSION_VALUES>& rgbConversionTable);

private:
    std::array<Rgb, NUM_PALETTE_COLORS> _palette;
    std::array<uint8_t, NUM_CONVERSION_VALUES> _rgbConversionTable;
};

} // namespace geck
