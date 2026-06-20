#pragma once

#include <istream>
#include <string>

#include "format/maps/MapsTxt.h"

namespace geck {

/// Parse Fallout 2's `data/maps.txt` — an INI file with one `[Map NNN]` section per map and
/// `key=value` lines (`;` begins a full-line comment). Reads the fields the engine reads in
/// `wmConfigInit` (fallout2-ce worldmap.cc): lookup_name, map_name, music, the yes/no flags and
/// ambient_sfx. Never throws — returns whatever parsed (an empty MapsTxt for empty/garbage input),
/// so a missing file just means destination maps are reported as bare indices.
MapsTxt parseMapsTxt(std::istream& in);
MapsTxt parseMapsTxt(const std::string& text);

} // namespace geck
