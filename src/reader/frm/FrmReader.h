#pragma once

#include <istream>
#include <string>

#include "reader/FileParser.h"

namespace geck {

class Frm;

class FrmReader : public FileParser<Frm> {
public:
    std::unique_ptr<Frm> read() override;
};

} // namespace geck
