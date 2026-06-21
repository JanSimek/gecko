#pragma once

#include <istream>
#include <string>

#include "format/city/CityTxt.h"

namespace geck {

/// Parse Fallout 2's `data/city.txt` — an INI file with one `[Area NN]` section per worldmap
/// location: `area_name`, `world_pos = x,y`, `start_state`, `size`, and `entrance_N = state, x, y,
/// map, elevation, tile, orientation` lines. Reads the fields the engine reads in `wmAreaInit`
/// (fallout2-ce worldmap.cc). Never throws — returns whatever parsed (an empty CityTxt for
/// empty/garbage input), so a missing file just means no world layer is available.
CityTxt parseCityTxt(std::istream& in);
CityTxt parseCityTxt(const std::string& text);

} // namespace geck
