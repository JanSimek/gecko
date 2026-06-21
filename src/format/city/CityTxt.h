#pragma once

#include <string>
#include <vector>

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
    std::string name;     ///< area_name
    int worldX = 0;       ///< world_pos x (worldmap position)
    int worldY = 0;       ///< world_pos y
    bool startOn = false; ///< start_state On = known/visible at game start
    std::string size;     ///< small / medium / large
    std::vector<CityEntrance> entrances;
};

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
