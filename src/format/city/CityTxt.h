#pragma once

#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/city.txt` — the worldmap area (location) registry.
///
/// `city.txt` is an INI-style file with one `[Area NN]` section per worldmap location; the
/// zero-padded `NN` fixes the area's index (an exit grid's "town map" / the worldmap reference it).
/// Inline `;` comments may appear on any line, **including section headers**, so a parser must strip
/// them first. Recognized keys:
///
/// @verbatim
/// [Area 00]                ; Arroyo
/// area_name=Arroyo         ; internal name (the displayed name comes from map.msg[1500 + index])
/// world_pos=184,133        ; x,y on the worldmap — the basis for distances between places
/// start_state=On           ; On = known/visible at game start
/// size=Medium              ; Small / Medium / Large
/// townmap_art_idx=156      ; intrface.lst index of the town-map background
/// townmap_label_art_idx=370; intrface.lst index of the town-map label strip
/// entrance_0=On,350,275,Arroyo Bridge,-1,-1,3
/// ;           state, x, y, map (a maps.txt lookup_name), elevation, tile, orientation
/// @endverbatim
///
/// The engine keys its area loop on `townmap_art_idx`: an `[Area NN]` without it ends the list.
///
/// @see fallout2-ce `worldmap.cc` (`wmAreaInit`)
/// @see https://fallout.wiki/wiki/CITY.TXT_File_Format

namespace geck {

/// One entrance of a worldmap area (city.txt `entrance_N = state, x, y, map, elevation, tile,
/// orientation`): which map the player drops into when entering this area, and where. `map` is the
/// destination map's lookup_name (e.g. "Arroyo Bridge"), matched to maps.txt by the engine.
struct CityEntrance {
    bool on = false;     ///< entrance enabled (state On/Off)
    int townX = -1;      ///< x on the town-map view
    int townY = -1;      ///< y on the town-map view
    std::string map;     ///< destination map lookup_name
    int elevation = -1;  ///< destination elevation (-1 = engine default)
    int tile = -1;       ///< destination tile (-1 = engine default)
    int orientation = 0; ///< facing on arrival
};

/// One `[Area NN]` of Fallout 2's `data/city.txt`: a worldmap location (a city/town/encounter site).
/// `worldX,worldY` is its position on the worldmap — the basis for straight-line distances between
/// places. Its `entrances` link it to the maps it contains. Mirrors the fields the engine reads in
/// `wmAreaInit` (fallout2-ce worldmap.cc). The display name may also live in worldmap.msg.
struct CityArea {
    int index = -1;
    std::string name;              ///< area_name
    int worldX = 0;                ///< world_pos x (worldmap position)
    int worldY = 0;                ///< world_pos y
    bool startOn = false;          ///< start_state On = known/visible at game start
    bool locked = false;           ///< lock_state On = the area cannot become known through play
    std::string size;              ///< small / medium / large
    int townMapArtIndex = -1;      ///< townmap_art_idx: intrface.lst index of the town-map background
    int townMapLabelArtIndex = -1; ///< townmap_label_art_idx: intrface.lst index of its label strip
    std::vector<CityEntrance> entrances;
};

/// The worldmap circle sizes a `size` key names, in the order the engine's `wmAreaSizeStrs` lists
/// them — the ordinal picks the marker art (`wrldspr0/1/2.frm`, 12/25/49 pixels square).
enum class CityAreaSize {
    Small = 0,
    Medium = 1,
    Large = 2,
};

/// Maps a `size` value (already lowercased by the reader) onto its ordinal. Unknown values fall
/// back to Large, matching the engine's `wmAreaSlotInit` default.
inline CityAreaSize cityAreaSize(std::string_view size) {
    using enum CityAreaSize;
    if (size == "small") {
        return Small;
    }
    if (size == "medium") {
        return Medium;
    }
    return Large;
}

/// The engine's own name for a size (`wmAreaSizeStrs`), for display.
inline const char* cityAreaSizeName(CityAreaSize size) {
    using enum CityAreaSize;
    switch (size) {
        case Small:
            return "small";
        case Medium:
            return "medium";
        case Large:
            break;
    }
    return "large";
}

/// Parsed `data/city.txt`: the worldmap areas, looked up by their `[Area NN]` index.
struct CityTxt {
    std::vector<CityArea> areas;

    const CityArea* find(int index) const {
        for (const auto& area : areas) {
            if (area.index == index) {
                return &area;
            }
        }
        return nullptr;
    }
};

} // namespace geck
