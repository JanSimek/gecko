#pragma once

#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/karmavar.txt` — the karma titles the character screen shows.
///
/// One entry per line, `gvar, art, name, description`, tokens separated by spaces, tabs or commas, a
/// line whose first non-blank character is `#` being a comment (fallout2-ce character_editor.cc
/// `karmaInit`). A title is listed while its gvar is non-zero; GVAR_PLAYER_REPUTATION is the
/// exception, shown as the karma value with a generic title picked by threshold.
///
/// @see fallout2-ce `character_editor.cc` (KarmaEntry)

namespace geck {

struct KarmaVar {
    int gvar = -1;        ///< the global variable holding the karma or reputation
    int art = -1;         ///< art/skilldex/skilldex.lst line of its picture
    int name = -1;        ///< editor.msg id of the title
    int description = -1; ///< editor.msg id of the description
};

/// Parsed `data/karmavar.txt`, in file order.
struct KarmaVarTxt {
    std::vector<KarmaVar> entries;
};

} // namespace geck
