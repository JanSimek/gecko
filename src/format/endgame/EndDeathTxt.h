#pragma once

#include <string>
#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/enddeath.txt` — the narrated death endings.
///
/// One ending per line, tokens separated by spaces, tabs or commas, a line whose first non-blank
/// character is `#` being a comment (fallout2-ce endgame.cc `endgameDeathEndingInit`):
///
/// @verbatim
/// # gvar, value, worldAreaKnown, worldAreaNotKnown, minLevel, %, narrator file
/// -1, 0, -1, 16, 0, 40, nar_dth3
/// 491, 1, 3, -1, 150, 0, nar_mo1
/// @endverbatim
///
/// When the player dies the engine keeps the lines whose conditions hold and picks one at random,
/// weighted by `percentage` (`endgameSetupDeathEnding`). The conditions (`endgameDeathEndingValidate`):
/// - gvar/value: when `gvar` is not -1 the line is **dropped once the gvar reaches `value`**. The
///   file's own header says the gvar "must be >=" — the engine does the opposite, and the engine is
///   what runs.
/// - areaKnown / areaNotKnown: city.txt area indices that must be known / not known (-1 = no check).
/// - minLevel: the player level the line needs.
///
/// @see fallout2-ce `endgame.cc` (EndgameDeathEnding)

namespace geck {

struct DeathEnding {
    int gvar = -1;         ///< global variable gating the line, -1 for none
    int value = 0;         ///< the line is dropped once `gvar` >= this
    int areaKnown = -1;    ///< city.txt area that must be known, -1 for none
    int areaNotKnown = -1; ///< city.txt area that must not be known, -1 for none
    int minLevel = 0;      ///< minimum player level
    int percentage = 0;    ///< weight in the random pick among the lines that qualify
    std::string narrator;  ///< base filename for the voice-over / subtitle
};

/// Parsed `data/enddeath.txt`: the death endings, in file order (the engine addresses them by index).
struct EndDeathTxt {
    std::vector<DeathEnding> endings;
};

} // namespace geck
