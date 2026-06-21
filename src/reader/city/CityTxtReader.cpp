#include "reader/city/CityTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>
#include <string>
#include <vector>

namespace geck {

namespace {

    using geck::text::toLower;
    using geck::text::trim;

    // city.txt uses inline ';' comments on almost every line (even section headers), so drop the
    // comment before any other parsing.
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

    int intOr(const std::string& s, int fallback) {
        try {
            return std::stoi(s);
        } catch (const std::exception&) {
            return fallback;
        }
    }

    // "[Area NN]" section name -> NN, or -1 if it isn't an area section.
    int parseAreaIndex(const std::string& section) {
        if (toLower(section).rfind("area", 0) != 0) {
            return -1;
        }
        return intOr(section.substr(4), -1); // after "Area", stoi skips the space and stops at the end
    }

    // entrance_N = state, townX, townY, map, elevation, tile, orientation
    CityEntrance parseEntrance(const std::string& value) {
        CityEntrance entrance;
        const auto f = splitCsv(value);
        if (f.size() > 0) {
            entrance.on = toLower(f[0]) == "on";
        }
        if (f.size() > 1) {
            entrance.townX = intOr(f[1], -1);
        }
        if (f.size() > 2) {
            entrance.townY = intOr(f[2], -1);
        }
        if (f.size() > 3) {
            entrance.map = f[3];
        }
        if (f.size() > 4) {
            entrance.elevation = intOr(f[4], -1);
        }
        if (f.size() > 5) {
            entrance.tile = intOr(f[5], -1);
        }
        if (f.size() > 6) {
            entrance.orientation = intOr(f[6], 0);
        }
        return entrance;
    }

    void applyField(CityArea& area, const std::string& key, const std::string& value) {
        if (key == "area_name") {
            area.name = value;
        } else if (key == "world_pos") {
            const auto xy = splitCsv(value);
            if (xy.size() > 0) {
                area.worldX = intOr(xy[0], 0);
            }
            if (xy.size() > 1) {
                area.worldY = intOr(xy[1], 0);
            }
        } else if (key == "start_state") {
            area.startOn = toLower(value) == "on";
        } else if (key == "size") {
            area.size = toLower(value);
        } else if (key.rfind("entrance_", 0) == 0) {
            area.entrances.push_back(parseEntrance(value));
        }
    }

} // namespace

CityTxt parseCityTxt(std::istream& in) {
    CityTxt out;
    CityArea current;
    bool inSection = false;
    const auto flush = [&] {
        if (inSection && current.index >= 0) {
            out.areas.push_back(current);
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        const std::string content = trim(stripComment(line));
        if (content.empty()) {
            continue;
        }
        if (content.front() == '[' && content.back() == ']') {
            flush();
            current = CityArea{};
            current.index = parseAreaIndex(trim(content.substr(1, content.size() - 2)));
            inSection = true;
            continue;
        }
        if (!inSection) {
            continue;
        }
        const auto eq = content.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        applyField(current, toLower(trim(content.substr(0, eq))), trim(content.substr(eq + 1)));
    }
    flush();
    return out;
}

CityTxt parseCityTxt(const std::string& text) {
    std::istringstream in(text);
    return parseCityTxt(in);
}

} // namespace geck
