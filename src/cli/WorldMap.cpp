#include "cli/WorldMap.h"

#include "cli/ConfigLoad.h"
#include "format/city/CityTxt.h"
#include "format/worldmap/WorldmapTxt.h"
#include "reader/city/CityTxtReader.h"
#include "reader/worldmap/WorldmapTxtReader.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    ordered_json areasToJson(const CityTxt& city, const resource::MapNameResolver& names, const WorldmapTxt& world) {
        auto areas = ordered_json::array();
        for (const auto& area : city.areas) {
            auto maps = ordered_json::array();
            for (const auto& entrance : area.entrances) {
                // mapFile resolves the entrance lookup_name to the .map filename, which is the join key
                // to map_graph nodes (null when the lookup_name has no maps.txt entry).
                const std::string mapFile = names.fileNameOfLookup(entrance.map);
                maps.push_back({ { "map", entrance.map }, { "on", entrance.on },
                    { "mapFile", mapFile.empty() ? ordered_json(nullptr) : ordered_json(mapFile) } });
            }
            const std::string terrain = world.terrainAt(area.worldX, area.worldY); // "" if no tile grid
            // The on-screen city label (map.msg[1500+index]) the engine shows, distinct from city.txt's
            // internal area_name in 'name'. null when map.msg isn't mounted or the area is unnamed.
            const std::string display = names.areaName(area.index);
            areas.push_back({ { "index", area.index }, { "name", area.name },
                { "displayName", display.empty() ? ordered_json(nullptr) : ordered_json(display) },
                { "worldPos", { { "x", area.worldX }, { "y", area.worldY } } },
                { "size", area.size }, { "knownAtStart", area.startOn },
                { "terrain", terrain.empty() ? ordered_json(nullptr) : ordered_json(terrain) },
                { "maps", std::move(maps) } });
        }
        return areas;
    }

    // Terrain-weighted travel cost: sample the straight line between two areas every subtile and sum
    // the terrain difficulty crossed. An approximation of the engine's path cost (it walks the real
    // route), but it reflects that mountains/ocean cost more to cross than open desert.
    double travelCost(const WorldmapTxt& world, int ax, int ay, int bx, int by) {
        const double dx = bx - ax;
        const double dy = by - ay;
        const int steps = std::max(1, static_cast<int>(std::sqrt(dx * dx + dy * dy) / WM_SUBTILE_SIZE));
        double cost = 0.0;
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const int px = static_cast<int>(std::lround(ax + dx * t));
            const int py = static_cast<int>(std::lround(ay + dy * t));
            cost += world.difficultyOf(world.terrainAt(px, py));
        }
        return cost;
    }

    // Where a new game begins. The engine hard-codes the first map (fallout2-ce main.cc:
    // `_mainMap = "artemple.map"`); resolve which worldmap area owns it + its map.msg name, so an
    // agent has the entry point of the start->objectives->ending chain.
    ordered_json startToJson(const CityTxt& city, const resource::MapNameResolver& names) {
        constexpr const char* kStartMap = "artemple.map";
        std::string area;
        for (const CityArea& a : city.areas) {
            for (const CityEntrance& entrance : a.entrances) {
                if (names.fileNameOfLookup(entrance.map) == kStartMap) {
                    area = a.name;
                }
            }
        }
        const std::string display = names.displayName(names.indexOf(kStartMap), 0);
        return { { "map", kStartMap },
            { "displayName", display.empty() ? ordered_json(nullptr) : ordered_json(display) },
            { "area", area.empty() ? ordered_json(nullptr) : ordered_json(area) } };
    }

    // Distance between every pair of areas: straight-line (as-the-crow-flies worldmap units) plus, when
    // the [Tile NN] terrain grid is present, a terrain-weighted travelCost (null otherwise).
    ordered_json distancesToJson(const CityTxt& city, const WorldmapTxt& world) {
        const bool hasGrid = world.numHorizontalTiles > 0 && !world.tiles.empty();
        auto distances = ordered_json::array();
        const auto& areas = city.areas;
        for (std::size_t i = 0; i < areas.size(); ++i) {
            for (std::size_t j = i + 1; j < areas.size(); ++j) {
                const double dx = static_cast<double>(areas[i].worldX - areas[j].worldX);
                const double dy = static_cast<double>(areas[i].worldY - areas[j].worldY);
                const double dist = std::round(std::sqrt(dx * dx + dy * dy) * 10.0) / 10.0;
                ordered_json travel = nullptr;
                if (hasGrid) {
                    const double cost = travelCost(world, areas[i].worldX, areas[i].worldY, areas[j].worldX, areas[j].worldY);
                    travel = std::round(cost * 10.0) / 10.0;
                }
                distances.push_back({ { "from", areas[i].index }, { "to", areas[j].index },
                    { "fromName", areas[i].name }, { "toName", areas[j].name }, { "distance", dist },
                    { "travelCost", travel } });
            }
        }
        return distances;
    }

} // namespace

int buildWorldMap(resource::GameResources& resources, std::ostream& out) {
    const CityTxt city = loadConfig<CityTxt>(resources, { "data/city.txt", "city.txt" },
        [](const std::string& text) { return parseCityTxt(text); });
    if (city.areas.empty()) {
        out << "{\"areas\":[],\"distances\":[],\"stats\":{\"areas\":0}}\n";
        return 1;
    }

    int knownAtStart = 0;
    for (const auto& area : city.areas) {
        if (area.startOn) {
            ++knownAtStart;
        }
    }

    // worldmap.txt is optional here: it enriches areas with terrain + adds terrain-weighted travelCost.
    const WorldmapTxt world = loadConfig<WorldmapTxt>(resources, { "data/worldmap.txt", "worldmap.txt" },
        [](const std::string& text) { return parseWorldmapTxt(text); });

    const resource::MapNameResolver names(resources); // resolves entrance lookup_names -> .map files
    ordered_json root;
    root["start"] = startToJson(city, names);
    root["areas"] = areasToJson(city, names, world);
    root["distances"] = distancesToJson(city, world);
    root["stats"] = { { "areas", city.areas.size() }, { "knownAtStart", knownAtStart } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
