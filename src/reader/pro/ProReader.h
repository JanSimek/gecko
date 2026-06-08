#pragma once

#include "reader/FileParser.h"

namespace geck {

class Pro;

class ProReader : public FileParser<Pro> {
public:
    std::unique_ptr<Pro> read() override;
};

} // namespace geck
