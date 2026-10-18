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
};

} // namespace geck
