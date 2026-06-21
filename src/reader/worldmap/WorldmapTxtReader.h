#pragma once

#include <istream>
#include <string>

#include "format/worldmap/WorldmapTxt.h"

namespace geck {

/// Parse Fallout 2's `data/worldmap.txt` — the `[Data]` terrain types and the `[Encounter: NAME]`
/// random-encounter group tables (`type_NN = Ratio:…, pid:…, …`). The large `[Tile NN]` sub-tile
/// grid is intentionally skipped (see WorldmapTxt). Never throws — returns whatever parsed (an empty
/// WorldmapTxt for empty/garbage input).
WorldmapTxt parseWorldmapTxt(std::istream& in);
WorldmapTxt parseWorldmapTxt(const std::string& text);

} // namespace geck
