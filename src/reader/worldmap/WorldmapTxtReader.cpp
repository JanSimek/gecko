#include "reader/worldmap/WorldmapTxtReader.h"

#include "reader/IniParser.h"
#include "reader/TextParsing.h"

#include <cstddef>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace geck {

namespace {

    using geck::text::intOr;
    using geck::text::splitCsv;
    using geck::text::toLower;
    using geck::text::trim;

    // The raw [Encounter Table N] keys, resolved once the whole file is read: the engine takes the
    // tables from 0 up to the first without a lookup_name, and each table's enc_NN from 00 up to the
    // first missing one.
    struct RawEncounterTable {
        std::string lookupName;
        std::string maps;
        std::map<int, std::string> entries;
    };

    // [Data]: terrain_types = "Desert:1, Mountain:2, …"; terrain_short_names = "DES, MNT, …" (aligned
    // to terrain_types by position). The frequency keys (None=0%, Rare=4%, …) are collected in
    // `frequencies` and resolved in engine order at the end.
    void applyDataField(WorldmapTxt& out, std::map<std::string, int, std::less<>>& frequencies, const std::string& key,
        const std::string& value) {
        for (const char* name : ENCOUNTER_FREQUENCY_NAMES) {
            if (key == name) {
                frequencies[key] = intOr(value, 0);
                return;
            }
        }
        if (key == "terrain_types") {
            for (const auto& pair : splitCsv(value)) {
                const auto colon = pair.find(':');
                WorldmapTerrain terrain;
                terrain.name = colon == std::string::npos ? pair : trim(pair.substr(0, colon));
                terrain.difficulty = colon == std::string::npos ? 0 : intOr(trim(pair.substr(colon + 1)), 0);
                out.terrains.push_back(terrain);
            }
        } else if (key == "terrain_short_names") {
            const auto shorts = splitCsv(value);
            for (std::size_t i = 0; i < shorts.size() && i < out.terrains.size(); ++i) {
                out.terrains[i].shortName = shorts[i];
            }
        }
    }

    EncounterFrequency frequencyOf(const std::string& field) {
        const std::string lower = toLower(field);
        for (std::size_t i = 0; i < ENCOUNTER_FREQUENCY_NAMES.size(); ++i) {
            if (lower == ENCOUNTER_FREQUENCY_NAMES[i]) {
                return static_cast<EncounterFrequency>(i);
            }
        }
        return EncounterFrequency::None;
    }

    // A [Tile NN] subtile line: key "row_column" (row 0-6, column 0-5) ->
    // "terrain, fill, morning, afternoon, night, table".
    void applyTileField(WorldmapTile& tile, const std::string& key, const std::string& value) {
        if (key == "art_idx") {
            tile.artIndex = intOr(value, -1);
            return;
        }
        if (key == "walk_mask_name") {
            tile.walkMaskName = value;
            return;
        }
        if (key == "encounter_difficulty") {
            tile.encounterDifficulty = intOr(value, 0);
            return;
        }

        const auto underscore = key.find('_');
        if (underscore == std::string::npos) {
            return;
        }
        const int row = intOr(key.substr(0, underscore), -1);
        const int col = intOr(key.substr(underscore + 1), -1);
        if (row < 0 || row >= SUBTILE_GRID_WIDTH || col < 0 || col >= SUBTILE_GRID_HEIGHT) {
            return;
        }
        const auto fields = splitCsv(value);
        WorldmapSubtile& subtile = tile.subtiles[row][col];
        if (!fields.empty()) {
            subtile.terrain = fields[0];
        }
        if (fields.size() > 1) {
            subtile.fill = toLower(fields[1]);
        }
        for (std::size_t part = 0; part < subtile.chance.size() && part + 2 < fields.size(); ++part) {
            subtile.chance[part] = frequencyOf(fields[part + 2]);
        }
        if (fields.size() > 5) {
            subtile.encounterTable = fields[5];
        }
    }

    // "Enc:(2-4) A AND (1-2) B FIGHTING Player": each group is an optional "(min-max)", a name and an
    // optional situation (fallout2-ce wmParseEncounterSubEncStr).
    std::vector<EncounterTableGroup> parseEncounterGroups(const std::string& text) {
        std::vector<EncounterTableGroup> groups;
        std::istringstream words(text);
        std::string word;
        EncounterTableGroup group;
        bool counted = false;
        while (words >> word) {
            const std::string lower = toLower(word);
            if (word.front() == '(') {
                const auto dash = word.find('-');
                group.minCount = intOr(word.substr(1), 1);
                group.maxCount = dash == std::string::npos ? group.minCount : intOr(word.substr(dash + 1), 1);
                counted = true;
            } else if (!groups.empty() && !counted && groups.back().situation == "nothing"
                && (lower == "ambush" || lower == "fighting" || lower == "and")) {
                groups.back().situation = lower;
            } else {
                group.name = word;
                groups.push_back(group);
                group = EncounterTableGroup{};
                counted = false;
            }
        }
        return groups;
    }

    // enc_NN = "Chance:50%,Enc:(2-5) Raiders AMBUSH Player,Scenery:Normal,If(Player(Level) > 4)".
    EncounterTableEntry parseEncounterTableEntry(const std::string& value) {
        EncounterTableEntry entry;
        for (const auto& field : splitCsv(value)) {
            const std::string lower = toLower(field);
            const auto colon = field.find(':');
            const std::string key = colon == std::string::npos ? lower : trim(lower.substr(0, colon));
            const std::string val = colon == std::string::npos ? std::string{} : trim(field.substr(colon + 1));
            if (lower.rfind("if", 0) == 0) {
                entry.condition = field;
            } else if (key == "special") {
                entry.special = true;
            } else if (key == "chance") {
                entry.chance = intOr(val, 0);
            } else if (key == "counter") {
                entry.counter = intOr(val, -1);
            } else if (key == "map") {
                entry.map = val;
            } else if (key == "enc") {
                entry.groups = parseEncounterGroups(val);
            } else if (key == "scenery") {
                entry.scenery = toLower(val);
            }
        }
        return entry;
    }

    std::vector<EncounterTable> resolveEncounterTables(const std::map<int, RawEncounterTable>& raw) {
        std::vector<EncounterTable> tables;
        for (int index = 0;; ++index) {
            const auto it = raw.find(index);
            if (it == raw.end() || it->second.lookupName.empty()) {
                break;
            }
            EncounterTable table;
            table.index = index;
            table.lookupName = it->second.lookupName;
            for (const auto& map : splitCsv(it->second.maps)) {
                if (!map.empty()) {
                    table.maps.push_back(map);
                }
            }
            for (int entry = 0;; ++entry) {
                const auto line = it->second.entries.find(entry);
                if (line == it->second.entries.end()) {
                    break;
                }
                table.entries.push_back(parseEncounterTableEntry(line->second));
            }
            table.skippedEntries = static_cast<int>(it->second.entries.size() - table.entries.size());
            tables.push_back(std::move(table));
        }
        return tables;
    }

    // type_NN = "Ratio:35%, pid:16777252, Item:8{Wielded}, Item:40, Script:255" (or a bare "Dead").
    EncounterEntry parseEncounterEntry(const std::string& value) {
        EncounterEntry entry;
        for (const auto& field : splitCsv(value)) {
            const auto colon = field.find(':');
            if (colon == std::string::npos) {
                if (toLower(field) == "dead") {
                    entry.dead = true;
                }
                continue;
            }
            const std::string key = toLower(trim(field.substr(0, colon)));
            const std::string val = trim(field.substr(colon + 1));
            if (key == "pid") {
                entry.pid = intOr(val, -1);
            } else if (key == "ratio") {
                entry.ratioPercent = intOr(val, 0);
            } else if (key == "script") {
                entry.script = intOr(val, -1);
            } else if (key == "distance") {
                entry.distance = intOr(val, -1);
            } else if (key == "item") {
                entry.items.push_back(intOr(val, -1));
            }
        }
        return entry;
    }

    enum class Mode {
        Other,
        Data,
        Encounter,
        EncounterTable,
        TileData,
        Tile
    };

    struct ParseState {
        Mode mode = Mode::Other;
        Encounter current;
        int currentTile = -1;
        std::map<std::string, int, std::less<>> frequencies;
        std::map<int, RawEncounterTable> tables;
        RawEncounterTable* currentTable = nullptr;
    };

    void flushEncounter(WorldmapTxt& out, ParseState& st) {
        if (st.mode == Mode::Encounter && !st.current.name.empty()) {
            out.encounters.push_back(st.current);
        }
    }

    // Start a new [Section]: flush any pending encounter, then switch parse mode.
    void selectSection(const std::string& section, WorldmapTxt& out, ParseState& st) {
        flushEncounter(out, st);
        const std::string lower = toLower(section);
        if (lower == "data") {
            st.mode = Mode::Data;
        } else if (lower == "tile data") {
            st.mode = Mode::TileData;
        } else if (lower.rfind("tile ", 0) == 0) {
            st.mode = Mode::Tile;
            st.currentTile = intOr(section.substr(std::string("tile ").size()), -1);
            if (st.currentTile >= 0 && st.currentTile >= static_cast<int>(out.tiles.size())) {
                out.tiles.resize(st.currentTile + 1);
            }
        } else if (lower.rfind("encounter table ", 0) == 0) {
            st.mode = Mode::EncounterTable;
            st.currentTable = &st.tables[intOr(trim(section.substr(std::string("encounter table ").size())), -1)];
        } else if (lower.rfind("encounter:", 0) == 0) {
            st.mode = Mode::Encounter;
            st.current = Encounter{};
            st.current.name = trim(section.substr(std::string("encounter:").size()));
        } else {
            st.mode = Mode::Other; // [Random Maps: …] etc. are not parsed
        }
    }

    // Apply a key=value line according to the current parse mode.
    void applyField(const std::string& key, const std::string& value, WorldmapTxt& out, ParseState& st) {
        switch (st.mode) {
            case Mode::Data:
                applyDataField(out, st.frequencies, key, value);
                break;
            case Mode::EncounterTable:
                if (key == "lookup_name") {
                    st.currentTable->lookupName = value;
                } else if (key == "maps") {
                    st.currentTable->maps = value;
                } else if (key.rfind("enc_", 0) == 0) {
                    st.currentTable->entries[intOr(key.substr(4), -1)] = value;
                }
                break;
            case Mode::TileData:
                if (key == "num_horizontal_tiles") {
                    out.numHorizontalTiles = intOr(value, 0);
                }
                break;
            case Mode::Tile:
                if (st.currentTile >= 0 && st.currentTile < static_cast<int>(out.tiles.size())) {
                    applyTileField(out.tiles[st.currentTile], key, value);
                }
                break;
            case Mode::Encounter:
                if (key.rfind("type_", 0) == 0) {
                    st.current.entries.push_back(parseEncounterEntry(value));
                }
                break;
            default:
                break;
        }
    }

} // namespace

WorldmapTxt parseWorldmapTxt(const std::string& text) {
    std::istringstream in(text);
    WorldmapTxt out;
    ParseState st;
    ini::parse(
        in,
        [&](const std::string& section) { selectSection(section, out, st); },
        [&](const std::string& key, const std::string& value) { applyField(key, value, out, st); });
    flushEncounter(out, st);
    for (std::size_t i = 0; i < ENCOUNTER_FREQUENCY_NAMES.size(); ++i) {
        const auto it = st.frequencies.find(ENCOUNTER_FREQUENCY_NAMES[i]);
        if (it == st.frequencies.end()) {
            break; // wmConfigInit stops at the first missing frequency
        }
        out.frequencyPercent[i] = it->second;
    }
    out.encounterTables = resolveEncounterTables(st.tables);
    return out;
}

} // namespace geck
