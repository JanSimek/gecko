#include "cli/WorldEncounters.h"

#include "cli/ConfigLoad.h"
#include "format/worldmap/WorldmapTxt.h"
#include "reader/worldmap/WorldmapTxtReader.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>

#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    ordered_json terrainsToJson(const WorldmapTxt& world) {
        auto terrains = ordered_json::array();
        for (const auto& terrain : world.terrains) {
            terrains.push_back({ { "name", terrain.name }, { "shortName", terrain.shortName }, { "weight", terrain.weight } });
        }
        return terrains;
    }

    ordered_json encountersToJson(const WorldmapTxt& world) {
        auto encounters = ordered_json::array();
        for (const auto& encounter : world.encounters) {
            auto entries = ordered_json::array();
            for (const auto& entry : encounter.entries) {
                entries.push_back({ { "pid", entry.pid }, { "ratioPercent", entry.ratioPercent },
                    { "dead", entry.dead }, { "script", entry.script }, { "items", entry.items } });
            }
            encounters.push_back({ { "name", encounter.name }, { "entries", std::move(entries) } });
        }
        return encounters;
    }

} // namespace

int buildWorldEncounters(resource::GameResources& resources, std::ostream& out) {
    const WorldmapTxt world = loadConfig<WorldmapTxt>(resources, { "data/worldmap.txt", "worldmap.txt" },
        [](const std::string& text) { return parseWorldmapTxt(text); });
    if (world.terrains.empty() && world.encounters.empty()) {
        out << "{\"terrains\":[],\"encounters\":[],\"stats\":{\"terrains\":0,\"encounters\":0}}\n";
        return 1;
    }

    ordered_json root;
    root["terrains"] = terrainsToJson(world);
    root["encounters"] = encountersToJson(world);
    root["stats"] = { { "terrains", world.terrains.size() }, { "encounters", world.encounters.size() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
