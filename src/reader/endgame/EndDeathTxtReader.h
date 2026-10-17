#pragma once

#include <istream>
#include <string>
#include <vector>

#include "format/endgame/EndDeathTxt.h"

namespace geck {

/// Parse Fallout 2's `data/enddeath.txt` — one death ending per line: `gvar, value, areaKnown,
/// areaNotKnown, minLevel, percentage, narrator`, separated by spaces, tabs or commas, with `#` starting
/// a comment. Lines with fewer than seven fields are skipped, as the engine does. Never throws.
EndDeathTxt parseEndDeathTxt(std::istream& in);
EndDeathTxt parseEndDeathTxt(const std::string& text);

/// Parse an ending subtitle file (`text/<lang>/cuts/<narrator>.txt`): one subtitle per line, the text
/// after the first `:` (the number before it is ignored, fallout2-ce endgame.cc
/// `endgameEndingSubtitlesLoad`). Lines without a `:` are skipped.
std::vector<std::string> parseEndingSubtitles(const std::string& text);

} // namespace geck
