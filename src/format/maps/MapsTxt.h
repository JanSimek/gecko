#pragma once

#include <string>
#include <utility>
#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/maps.txt` — the engine's index→map registry.
///
/// `maps.txt` is an INI-style file with one `[Map NNN]` section per map; the zero-padded `NNN` is the
/// map's index — the number an exit grid's `destMap` references (so `destMap` 3 is `[Map 003]`).
/// Recognized keys (the engine reads more; `map_name` is stored without an extension and lowercased):
///
/// @verbatim
/// [Map 003]
/// lookup_name=Arroyo Caves   ; internal area key (city.txt entrances match maps by this)
/// map_name=arcaves           ; the .map filename (no extension in the file)
/// music=02Desert
/// ambient_sfx=brmnwind:35, coyote:10   ; sound:chance pairs
/// saved=No
/// pipboy_active=Yes
/// @endverbatim
///
/// @see fallout2-ce `worldmap.cc` (`wmConfigInit`)
/// @see https://fallout.wiki/wiki/MAPS.TXT_File_Format

namespace geck {

/// One `[Map NNN]` section of Fallout 2's `data/maps.txt` — the engine's index→map registry that an
/// exit grid's destination map number indexes into (so a `destMap` of 3 is `[Map 003]`). Mirrors the
/// fields the engine reads in `wmConfigInit` (fallout2-ce worldmap.cc).
struct MapInfo {
    int index = -1;             ///< the NNN in `[Map NNN]`
    std::string lookupName;     ///< lookup_name — the engine's internal area key
    std::string mapName;        ///< map_name normalized to a lowercase, loadable "<name>.map" filename
    std::string music;          ///< music track name
    bool saved = false;         ///< saved — map state persists across visits
    bool deadBodiesAge = false; ///< dead_bodies_age
    bool pipboyActive = false;  ///< pipboy_active
    bool automap = false;       ///< automap
    /// ambient_sfx — sound-effect name → percent chance pairs.
    std::vector<std::pair<std::string, int>> ambientSfx;
};

/// Parsed `data/maps.txt`: the ordered list of map sections, looked up by their `[Map NNN]` index.
struct MapsTxt {
    std::vector<MapInfo> maps;

    /// The section with the given index, or nullptr if absent.
    const MapInfo* find(int index) const {
        for (const auto& map : maps) {
            if (map.index == index) {
                return &map;
            }
        }
        return nullptr;
    }

    /// The section whose map_name matches `mapName`, or nullptr. `mapName` must be a lowercase .map
    /// filename (as the reader stores it) — e.g. the basename of the map being analysed, lowercased.
    const MapInfo* findByName(const std::string& mapName) const {
        for (const auto& map : maps) {
            if (map.mapName == mapName) {
                return &map;
            }
        }
        return nullptr;
    }
};

} // namespace geck
