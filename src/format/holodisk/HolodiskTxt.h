#pragma once

#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/holodisk.txt` — which global variable owns each Pip-Boy holodisk.
///
/// One holodisk per line, `gvar, title, text`, tokens separated by spaces, tabs or commas, a line
/// whose first non-blank character is `#` being a comment (fallout2-ce pipboy.cc `holodiskInit`).
/// The Pip-Boy lists a holodisk once its gvar is non-zero. `title` is a pipboy.msg id; the text is the
/// pipboy.msg messages from `text` up to `**END-DISK**`, at most 500 of them.
///
/// @see fallout2-ce `pipboy.cc` (HolodiskDescription)

namespace geck {

struct Holodisk {
    int gvar = -1;  ///< the global variable that is non-zero once the player has the disk
    int title = -1; ///< pipboy.msg id of the title
    int text = -1;  ///< pipboy.msg id of the first text line
};

/// Parsed `data/holodisk.txt`, in file order.
struct HolodiskTxt {
    std::vector<Holodisk> holodisks;
};

} // namespace geck
