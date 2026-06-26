#pragma once

#include "reader/FileParser.h"

#include <string>

namespace geck {

class Gam;

/// The one reader for a `.gam`. It has two entry points onto the same lossless parse:
///
///  - @ref parse — turns a `.gam`'s text into the lossless @ref Gam model and never throws. The Map Info
///    panel calls this on already-loaded bytes so it can edit a map's MAP_GLOBAL_VARS and write the file
///    back untouched.
///  - the @ref FileParser `read` path (used by @ref ReaderFactory / the resource cache) — validates that
///    the file is a real `.gam` (non-empty, has a GAME_GLOBAL_VARS/MAP_GLOBAL_VARS section, no variable
///    declared outside a section) and throws otherwise, since the cache treats a load failure as
///    "not a .gam".
class GamReader : public FileParser<Gam> {
public:
    /// Parse a `.gam`'s whole text into a lossless @ref Gam (every line in file order, tagged by kind and
    /// section; Variable lines split into name/value plus the prefix/suffix around the integer so an edit
    /// rebuilds the line). A trailing `\r` is stripped per line. Never throws.
    static Gam parse(const std::string& content);

    std::unique_ptr<Gam> read() override;
};

} // namespace geck
