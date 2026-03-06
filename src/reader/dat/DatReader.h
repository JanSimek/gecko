#pragma once

#include <string>

#include "../FileParser.h"

namespace geck {

class Dat;

class DatReader : public FileParser<Dat> {
private:
    std::string _file;

    // DAT2 offsets and data sizes
    static constexpr int FILE_COUNT_SIZE_FIELD = 4; //!< Size of DirTree in bytes
    static constexpr int TREE_SIZE_FIELD = 4;       //!< Size of DirTree in bytes
    static constexpr int DATA_SIZE_FIELD = 4;       //!< Full size of the archive in bytes
    static constexpr int FOOTER_SIZE = TREE_SIZE_FIELD + DATA_SIZE_FIELD;

public:
    DatReader()
        : FileParser(StreamBuffer::ENDIANNESS::LITTLE) { }
    virtual ~DatReader() = default;
    std::unique_ptr<Dat> read() override;
};

} // namespace geck
