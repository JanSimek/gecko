#pragma once

#include "reader/FileParser.h"

namespace geck {

class Pal;

class PalReader : public FileParser<Pal> {
public:
    std::unique_ptr<Pal> read() override;
};

} // namespace geck
