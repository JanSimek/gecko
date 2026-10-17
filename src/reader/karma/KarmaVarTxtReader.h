#pragma once

#include <istream>
#include <string>

#include "format/karma/KarmaVarTxt.h"

namespace geck {

/// Parse Fallout 2's `data/karmavar.txt` — one karma title per line: `gvar, art, name, description`,
/// separated by spaces, tabs or commas, with `#` starting a comment. Lines with fewer than four fields
/// are skipped, as the engine does. Never throws.
KarmaVarTxt parseKarmaVarTxt(std::istream& in);
KarmaVarTxt parseKarmaVarTxt(const std::string& text);

} // namespace geck
