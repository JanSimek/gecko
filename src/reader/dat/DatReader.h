#pragma once

#include <cstdint>

#include "reader/FileParser.h"

namespace geck {

class Dat;

class DatReader : public FileParser<Dat> {
private:
    // DAT2 offsets and data sizes
    static constexpr uint32_t TREE_SIZE_FIELD = 4; //!< Size of DirTree in bytes
    static constexpr uint32_t DATA_SIZE_FIELD = 4; //!< Full size of the archive in bytes
    static constexpr uint32_t FOOTER_SIZE = TREE_SIZE_FIELD + DATA_SIZE_FIELD;

public:
    DatReader()
        : FileParser(StreamBuffer::ENDIANNESS::LITTLE) { }
    virtual ~DatReader() = default;
    std::unique_ptr<Dat> read() override;

    // Fallout's LZSS as DAT1 packs it: blocks led by a big-endian u16; with
    // the top bit set the block is stored, otherwise LZSS (4096-byte window
    // of spaces, write position 4078, match length low nibble + 3, one flag
    // byte per eight items, 1 = literal), the window fresh for every block.
    static void unpackLzss(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize);

private:
    bool isDat2();
    std::unique_ptr<Dat> readDat1();
};

} // namespace geck
