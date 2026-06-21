#include "reader/worldmap/WorldmapTxtReader.h"

#include "reader/TextParsing.h"

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace geck {

namespace {

    using geck::text::toLower;
    using geck::text::trim;

    // worldmap.txt uses inline ';' comments throughout (even on section headers), so drop the comment
    // before any other parsing.
    std::string stripComment(const std::string& line) {
        return line.substr(0, line.find(';'));
    }

    std::vector<std::string> splitCsv(const std::string& s) {
        std::vector<std::string> out;
        std::istringstream stream(s);
        std::string item;
        while (std::getline(stream, item, ',')) {
            out.push_back(trim(item));
        }
        return out;
    }

    // stoi that stops at the first non-digit, so "35%" -> 35 and "8{Wielded}" -> 8; fallback on junk.
    int intOr(const std::string& s, int fallback) {
        try {
            return std::stoi(s);
        } catch (const std::exception&) {
            return fallback;
        }
    }

    // [Data]: terrain_types = "Desert:1, Mountain:2, …"; terrain_short_names = "DES, MNT, …" (aligned
    // to terrain_types by position).
    void applyDataField(WorldmapTxt& out, const std::string& key, const std::string& value) {
        if (key == "terrain_types") {
            for (const auto& pair : splitCsv(value)) {
                const auto colon = pair.find(':');
                WorldmapTerrain terrain;
                terrain.name = colon == std::string::npos ? pair : trim(pair.substr(0, colon));
                terrain.weight = colon == std::string::npos ? 0 : intOr(trim(pair.substr(colon + 1)), 0);
                out.terrains.push_back(terrain);
            }
        } else if (key == "terrain_short_names") {
            const auto shorts = splitCsv(value);
            for (std::size_t i = 0; i < shorts.size() && i < out.terrains.size(); ++i) {
                out.terrains[i].shortName = shorts[i];
            }
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
        Encounter
    };

} // namespace

WorldmapTxt parseWorldmapTxt(std::istream& in) {
    WorldmapTxt out;
    Mode mode = Mode::Other;
    Encounter current;
    const auto flushEncounter = [&] {
        if (mode == Mode::Encounter && !current.name.empty()) {
            out.encounters.push_back(current);
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        const std::string content = trim(stripComment(line));
        if (content.empty()) {
            continue;
        }
        if (content.front() == '[' && content.back() == ']') {
            flushEncounter();
            const std::string section = trim(content.substr(1, content.size() - 2));
            const std::string lower = toLower(section);
            if (lower == "data") {
                mode = Mode::Data;
            } else if (lower.rfind("encounter:", 0) == 0) {
                mode = Mode::Encounter;
                current = Encounter{};
                current.name = trim(section.substr(std::string("encounter:").size()));
            } else {
                mode = Mode::Other; // [Random Maps: …], [Tile Data], [Tile NN], … are not parsed
            }
            continue;
        }
        const auto eq = content.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = toLower(trim(content.substr(0, eq)));
        const std::string value = trim(content.substr(eq + 1));
        if (mode == Mode::Data) {
            applyDataField(out, key, value);
        } else if (mode == Mode::Encounter && key.rfind("type_", 0) == 0) {
            current.entries.push_back(parseEncounterEntry(value));
        }
    }
    flushEncounter();
    return out;
}

WorldmapTxt parseWorldmapTxt(const std::string& text) {
    std::istringstream in(text);
    return parseWorldmapTxt(in);
}

} // namespace geck
