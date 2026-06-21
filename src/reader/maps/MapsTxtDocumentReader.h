#pragma once

#include "format/maps/MapsTxtDocument.h"

#include <string>

namespace geck {

/// Parse the whole of `data/maps.txt` into a lossless @ref MapsTxtDocument: the preamble, every
/// `[Map NNN]` section in order, and within each section every line (blanks, comments, commented-out
/// keys, repeated/multi-value keys, and keys the typed reader doesn't model) — each keeping its raw
/// content. A trailing `\r` is stripped per line; `finalNewline` records the trailing newline. Never
/// throws.
MapsTxtDocument parseMapsTxtDocument(const std::string& content);

} // namespace geck
