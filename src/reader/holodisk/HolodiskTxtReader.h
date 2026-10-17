#pragma once

#include <istream>
#include <string>

#include "format/holodisk/HolodiskTxt.h"

namespace geck {

/// Parse Fallout 2's `data/holodisk.txt` — one holodisk per line: `gvar, title, text`, separated by
/// spaces, tabs or commas, with `#` starting a comment. Lines with fewer than three fields are skipped,
/// as the engine does. Never throws.
HolodiskTxt parseHolodiskTxt(std::istream& in);
HolodiskTxt parseHolodiskTxt(const std::string& text);

} // namespace geck
