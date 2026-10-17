#include "cli/WorldEncounters.h"

#include "resource/ConfigLoad.h"
#include "cli/GlobalVars.h" // loadGameGam
#include "format/gam/Gam.h"
#include "format/worldmap/WorldmapTxt.h"
#include "reader/worldmap/WorldmapTxtReader.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <format>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    WorldmapTxt loadWorldmap(resource::GameResources& resources) {
        return resource::loadConfig<WorldmapTxt>(resources, { "data/worldmap.txt", "worldmap.txt" },
            [](const std::string& text) { return parseWorldmapTxt(text); });
    }

    ordered_json terrainsToJson(const WorldmapTxt& world) {
        auto terrains = ordered_json::array();
        for (const auto& terrain : world.terrains) {
            terrains.push_back({ { "name", terrain.name }, { "shortName", terrain.shortName }, { "difficulty", terrain.difficulty } });
        }
        return terrains;
    }

    ordered_json frequenciesToJson(const WorldmapTxt& world) {
        ordered_json frequencies = ordered_json::object();
        for (std::size_t i = 0; i < ENCOUNTER_FREQUENCY_NAMES.size(); ++i) {
            frequencies[ENCOUNTER_FREQUENCY_NAMES[i]] = world.frequencyPercent[i];
        }
        return frequencies;
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

    // The gvars an If(…) chain reads through Global(N), named through vault13.gam when it's mounted.
    ordered_json conditionGvars(const Gam* gam, std::string_view condition) {
        using Iterator = std::regex_iterator<std::string_view::const_iterator>;
        static const std::regex global(R"(Global\((\d+)\))", std::regex::icase);
        auto gvars = ordered_json::array();
        for (auto it = Iterator(condition.begin(), condition.end(), global); it != Iterator(); ++it) {
            const auto index = static_cast<std::size_t>(std::stoul((*it)[1].str()));
            const bool named = gam != nullptr && index < gam->gvarCount();
            gvars.push_back({ { "index", index }, { "name", named ? ordered_json(gam->gvarKey(index)) : ordered_json(nullptr) } });
        }
        return gvars;
    }

    // A maps.txt lookup_name with the .map file it resolves to (null when maps.txt has no such entry),
    // the same join key world_map gives its area maps.
    ordered_json mapJson(const resource::MapNameResolver& names, const std::string& lookupName) {
        const std::string file = names.fileNameOfLookup(lookupName);
        return { { "map", lookupName }, { "mapFile", file.empty() ? ordered_json(nullptr) : ordered_json(file) } };
    }

    ordered_json tableToJson(const EncounterTable& table, const Gam* gam, const resource::MapNameResolver& names) {
        auto entries = ordered_json::array();
        for (const auto& entry : table.entries) {
            auto groups = ordered_json::array();
            for (const auto& group : entry.groups) {
                groups.push_back({ { "name", group.name }, { "minCount", group.minCount }, { "maxCount", group.maxCount },
                    { "situation", group.situation } });
            }
            ordered_json json = { { "chance", entry.chance }, { "counter", entry.counter }, { "special", entry.special },
                { "map", entry.map.empty() ? ordered_json(nullptr) : mapJson(names, entry.map) }, { "groups", std::move(groups) },
                { "scenery", entry.scenery } };
            if (!entry.condition.empty()) {
                json["condition"] = entry.condition;
                json["conditionGvars"] = conditionGvars(gam, entry.condition);
            }
            entries.push_back(std::move(json));
        }
        auto maps = ordered_json::array();
        for (const auto& map : table.maps) {
            maps.push_back(mapJson(names, map));
        }
        ordered_json json = { { "index", table.index }, { "lookupName", table.lookupName }, { "maps", std::move(maps) },
            { "entries", std::move(entries) } };
        if (table.skippedEntries > 0) {
            json["skippedEntries"] = table.skippedEntries; // past a gap in the enc_NN numbering, never rolled
        }
        return json;
    }

    ordered_json tablesToJson(const WorldmapTxt& world, const Gam* gam, const resource::MapNameResolver& names) {
        auto tables = ordered_json::array();
        for (const auto& table : world.encounterTables) {
            tables.push_back(tableToJson(table, gam, names));
        }
        return tables;
    }

} // namespace

int buildWorldEncounters(resource::GameResources& resources, std::ostream& out) {
    const WorldmapTxt world = loadWorldmap(resources);
    if (world.terrains.empty() && world.encounters.empty()) {
        out << "{\"terrains\":[],\"encounters\":[],\"encounterTables\":[],\"stats\":{\"terrains\":0,\"encounters\":0,"
               "\"encounterTables\":0}}\n";
        return 1;
    }

    ordered_json root;
    root["terrains"] = terrainsToJson(world);
    root["frequencies"] = frequenciesToJson(world);
    root["encounters"] = encountersToJson(world);
    root["encounterTables"] = tablesToJson(world, loadGameGam(resources), resource::MapNameResolver(resources));
    root["stats"] = { { "terrains", world.terrains.size() }, { "encounters", world.encounters.size() },
        { "encounterTables", world.encounterTables.size() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

int describeWorldPosition(resource::GameResources& resources, int x, int y, std::ostream& out) {
    const WorldmapTxt world = loadWorldmap(resources);
    const WorldmapSubtile* subtile = world.subtileAt(x, y);
    if (subtile == nullptr) {
        const ordered_json error = { { "error", std::format("no worldmap sub-tile at ({}, {})", x, y) },
            { "width", world.widthPixels() }, { "height", world.heightPixels() } };
        out << error.dump() << "\n";
        return 1;
    }

    ordered_json chances = ordered_json::object();
    for (std::size_t part = 0; part < DAY_PART_NAMES.size(); ++part) {
        const auto frequency = static_cast<std::size_t>(subtile->chance[part]);
        chances[DAY_PART_NAMES[part]] = { { "frequency", ENCOUNTER_FREQUENCY_NAMES[frequency] },
            { "percent", world.frequencyPercent[frequency] } };
    }

    const int tile = world.tileIndexAt(x, y);
    const EncounterTable* table = world.findEncounterTable(subtile->encounterTable);
    ordered_json root;
    root["x"] = x;
    root["y"] = y;
    root["tile"] = tile;
    root["subtile"] = { { "row", (x % WM_TILE_WIDTH) / WM_SUBTILE_SIZE }, { "column", (y % WM_TILE_HEIGHT) / WM_SUBTILE_SIZE } };
    root["terrain"] = subtile->terrain;
    root["fill"] = subtile->fill;
    root["encounterDifficulty"] = world.tiles[static_cast<std::size_t>(tile)].encounterDifficulty;
    root["chances"] = std::move(chances);
    root["encounterTableName"] = subtile->encounterTable;
    root["encounterTable"] = table != nullptr ? tableToJson(*table, loadGameGam(resources), resource::MapNameResolver(resources))
                                              : ordered_json(nullptr);
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
