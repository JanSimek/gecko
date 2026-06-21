#pragma once

#include <string>
#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/endgame.txt` — the endgame slideshow's win-condition table.
///
/// One slide per line, comma-separated, `#` starting a comment (fallout2-ce endgame.cc
/// `endgameEndingInit`): a slide is shown when its global variable equals its `value`. Several lines
/// sharing a gvar with different values are the branching outcomes for one location.
///
/// @verbatim
/// # gvar, value, art_number, narrator_file [, direction]
/// 408, 1, 440, nar_ar1            ; if GVAR 408 == 1, show intrface.lst art 440 with voice-over nar_ar1
/// 40,  1, 327, nar_10, 1          ; direction only applies to the panning-desert art (327)
/// @endverbatim
///
/// - gvar/value: the slide shows when global var `gvar` equals `value`.
/// - art: line in `art/intrface/intrface.lst` for the slide image.
/// - narrator: base filename for the voice-over + subtitle (`text/<lang>/cuts/<narrator>.txt`).
/// - direction: pan direction (-1/1) for the panning-desert image only.
///
/// @see fallout2-ce `endgame.cc` (EndgameEnding, endgameEndingInit)
/// @see https://fallout.wiki/wiki/ENDGAME.TXT_File_Format

namespace geck {

struct Ending {
    int gvar = -1;        ///< the global variable controlling this slide
    int value = 0;        ///< the gvar value that triggers the slide
    int art = -1;         ///< intrface.lst line of the slide image
    std::string narrator; ///< base filename for the voice-over / subtitle
    int direction = 0;    ///< pan direction for the panning-desert art (327); 0 if unused
};

/// Parsed `data/endgame.txt`: the ending slides, in file order.
struct EndgameTxt {
    std::vector<Ending> endings;
};

} // namespace geck
