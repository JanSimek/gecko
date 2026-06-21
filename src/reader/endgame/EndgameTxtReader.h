#pragma once

#include <istream>
#include <string>

#include "format/endgame/EndgameTxt.h"

namespace geck {

/// Parse Fallout 2's `data/endgame.txt` — one ending slide per line: `gvar, value, art, narrator
/// [, direction]`, comma-separated, with `#` starting a comment. Lines with fewer than four fields
/// are skipped. Never throws — returns whatever parsed.
EndgameTxt parseEndgameTxt(std::istream& in);
EndgameTxt parseEndgameTxt(const std::string& text);

} // namespace geck
