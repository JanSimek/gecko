#include "reader/worldmap/WorldmapTxtReader.h"

#include "reader/IniParser.h"
#include "reader/TextParsing.h"

#include <cstddef>
#include <sstream>
#include <string>

namespace geck {

namespace {

    using geck::text::intOr;
    using geck::text::splitCsv;
    using geck::text::toLower;
    using geck::text::trim;

    // [Data]: terrain_types = "Desert:1, Mountain:2, …"; terrain_short_names = "DES, MNT, …" (aligned
    // to terrain_types by position).
    void applyDataField(WorldmapTxt& out, const std::string& key, const std::string& value) {
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

    // A [Tile NN] subtile line: key "row_column" (row 0-6, column 0-5) -> "terrain, fill, chances, …".
    // We keep the terrain (the first field).
    void applyTileField(WorldmapTile& tile, const std::string& key, const std::string& value) {
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
        if (!fields.empty()) {
            tile.terrain[row][col] = fields[0];
        }
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
        TileData,
        Tile
    };

    struct ParseState {
        Mode mode = Mode::Other;
        Encounter current;
        int currentTile = -1;
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
                applyDataField(out, key, value);
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

WorldmapTxt parseWorldmapTxt(std::istream& in) {
    WorldmapTxt out;
    ParseState st;
    ini::parse(
        in,
        [&](const std::string& section) { selectSection(section, out, st); },
        [&](const std::string& key, const std::string& value) { applyField(key, value, out, st); });
    flushEncounter(out, st);
    return out;
}

WorldmapTxt parseWorldmapTxt(const std::string& text) {
    std::istringstream in(text);
    return parseWorldmapTxt(in);
}

} // namespace geck
