#pragma once

#include <string>
#include <cstdint>

#include "Dat.h"

namespace geck {

class DatEntry {
private:
    std::string filename;

    bool compressed;
    uint32_t decompressedSize;
    uint32_t packedSize;

    uint32_t offset;

    bool lzss = false; // DAT1 (Fallout 1): packed with Fallout's LZSS, not zlib

public:
    virtual ~DatEntry() = default;

    std::string getFilename() const;
    void setFilename(const std::string& value);

    bool getCompressed() const;
    void setCompressed(bool value);

    uint32_t getDecompressedSize() const;
    void setDecompressedSize(const uint32_t& value);

    uint32_t getPackedSize() const;
    void setPackedSize(const uint32_t& value);

    uint32_t getOffset() const;
    void setOffset(const uint32_t& value);

    bool getLzss() const { return lzss; }
    void setLzss(bool value) { lzss = value; }
};

} // namespace geck
