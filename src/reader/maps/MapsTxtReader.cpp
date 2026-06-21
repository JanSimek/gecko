#include "reader/maps/MapsTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>
#include <utility>

namespace geck {

namespace {

    using geck::text::toLower;
    using geck::text::trim;

    // The engine treats the first word as a yes/no flag (wmYesNoStrs); be lenient about 1/true too.
    bool parseYesNo(const std::string& value) {
        const std::string v = toLower(trim(value));
        return v == "yes" || v == "1" || v == "true";
    }

    // ambient_sfx is "name:chance, name:chance, …"; a missing/garbage chance is left at 0.
    std::vector<std::pair<std::string, int>> parseAmbientSfx(const std::string& value) {
        std::vector<std::pair<std::string, int>> out;
        std::istringstream items(value);
        std::string item;
        while (std::getline(items, item, ',')) {
            const std::string entry = trim(item);
            if (entry.empty()) {
                continue;
            }
            const auto colon = entry.find(':');
            std::string name = colon == std::string::npos ? entry : trim(entry.substr(0, colon));
            int chance = 0;
            if (colon != std::string::npos) {
                try {
                    chance = std::stoi(trim(entry.substr(colon + 1)));
                } catch (const std::exception&) {
                    chance = 0;
                }
            }
            if (!name.empty()) {
                out.emplace_back(std::move(name), chance);
            }
        }
        return out;
    }

    void applyField(MapInfo& map, const std::string& key, const std::string& value) {
        if (key == "lookup_name") {
            map.lookupName = value;
        } else if (key == "map_name") {
            // maps.txt stores the bare name ("arcaves"); the engine lowercases it and appends ".map"
            // when loading. Normalize to the loadable "<name>.map" so mapName is a real filename that
            // matches a map path's basename (and can be handed back to load/analyze).
            std::string name = toLower(value);
            if (name.find('.') == std::string::npos) {
                name += ".map";
            }
            map.mapName = name;
        } else if (key == "music") {
            map.music = value;
        } else if (key == "saved") {
            map.saved = parseYesNo(value);
        } else if (key == "dead_bodies_age") {
            map.deadBodiesAge = parseYesNo(value);
        } else if (key == "pipboy_active") {
            map.pipboyActive = parseYesNo(value);
        } else if (key == "automap") {
            map.automap = parseYesNo(value);
        } else if (key == "ambient_sfx") {
            map.ambientSfx = parseAmbientSfx(value);
        }
    }

    // "[Map NNN]" section name (the text inside the brackets) -> NNN, or -1 if it isn't a map section.
    int parseSectionIndex(const std::string& section) {
        if (toLower(section).rfind("map", 0) != 0) {
            return -1;
        }
        try {
            return std::stoi(section.substr(3)); // stoi skips the leading space and stops at the ']'
        } catch (const std::exception&) {
            return -1;
        }
    }

} // namespace

MapsTxt parseMapsTxt(std::istream& in) {
    MapsTxt out;
    MapInfo current;
    bool inSection = false;
    const auto flush = [&] {
        if (inSection && current.index >= 0) {
            out.maps.push_back(current);
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            flush();
            current = MapInfo{};
            current.index = parseSectionIndex(trim(trimmed.substr(1, trimmed.size() - 2)));
            inSection = true;
            continue;
        }
        if (!inSection) {
            continue; // a stray key before the first section
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        applyField(current, toLower(trim(trimmed.substr(0, eq))), trim(trimmed.substr(eq + 1)));
    }
    flush();
    return out;
}

MapsTxt parseMapsTxt(const std::string& text) {
    std::istringstream in(text);
    return parseMapsTxt(in);
}

} // namespace geck
