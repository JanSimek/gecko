#include "cli/WorldMap.h"

#include "format/city/CityTxt.h"
#include "reader/city/CityTxtReader.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    CityTxt loadCityTxt(resource::GameResources& resources) {
        for (const char* path : { "data/city.txt", "city.txt" }) {
            if (const auto bytes = resources.files().readRawBytes(path); bytes.has_value()) {
                return parseCityTxt(std::string(bytes->begin(), bytes->end()));
            }
        }
        return CityTxt{};
    }

    ordered_json areasToJson(const CityTxt& city) {
        auto areas = ordered_json::array();
        for (const auto& area : city.areas) {
            auto maps = ordered_json::array();
            for (const auto& entrance : area.entrances) {
                maps.push_back({ { "map", entrance.map }, { "on", entrance.on } });
            }
            areas.push_back({ { "index", area.index }, { "name", area.name },
                { "worldPos", { { "x", area.worldX }, { "y", area.worldY } } },
                { "size", area.size }, { "knownAtStart", area.startOn }, { "maps", std::move(maps) } });
        }
        return areas;
    }

    // Straight-line distance (in worldmap units) between every pair of areas. Terrain-weighted travel
    // cost would need worldmap.txt; this is the as-the-crow-flies "distance between places".
    ordered_json distancesToJson(const CityTxt& city) {
        auto distances = ordered_json::array();
        const auto& areas = city.areas;
        for (std::size_t i = 0; i < areas.size(); ++i) {
            for (std::size_t j = i + 1; j < areas.size(); ++j) {
                const double dx = static_cast<double>(areas[i].worldX - areas[j].worldX);
                const double dy = static_cast<double>(areas[i].worldY - areas[j].worldY);
                const double dist = std::round(std::sqrt(dx * dx + dy * dy) * 10.0) / 10.0;
                distances.push_back({ { "from", areas[i].index }, { "to", areas[j].index },
                    { "fromName", areas[i].name }, { "toName", areas[j].name }, { "distance", dist } });
            }
        }
        return distances;
    }

} // namespace

int buildWorldMap(resource::GameResources& resources, std::ostream& out) {
    const CityTxt city = loadCityTxt(resources);
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

    ordered_json root;
    root["areas"] = areasToJson(city);
    root["distances"] = distancesToJson(city);
    root["stats"] = { { "areas", city.areas.size() }, { "knownAtStart", knownAtStart } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
