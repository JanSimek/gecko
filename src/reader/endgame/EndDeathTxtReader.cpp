#include "reader/endgame/EndDeathTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>
#include <string>

namespace geck {

namespace {
    using geck::text::intOr;
    using geck::text::splitTokens;
    using geck::text::trim;
} // namespace

EndDeathTxt parseEndDeathTxt(std::istream& in) {
    EndDeathTxt out;
    std::string line;
    while (std::getline(in, line)) {
        const std::string content = trim(line.substr(0, line.find('#'))); // '#' starts a comment
        if (content.empty()) {
            continue;
        }
        const auto tokens = splitTokens(content); // gvar, value, areaKnown, areaNotKnown, minLevel, %, narrator
        if (tokens.size() < 7) {
            continue;
        }
        DeathEnding ending;
        ending.gvar = intOr(tokens[0], -1);
        ending.value = intOr(tokens[1], 0);
        ending.areaKnown = intOr(tokens[2], -1);
        ending.areaNotKnown = intOr(tokens[3], -1);
        ending.minLevel = intOr(tokens[4], 0);
        ending.percentage = intOr(tokens[5], 0);
        ending.narrator = tokens[6];
        out.endings.push_back(ending);
    }
    return out;
}

EndDeathTxt parseEndDeathTxt(const std::string& text) {
    std::istringstream in(text);
    return parseEndDeathTxt(in);
}

std::vector<std::string> parseEndingSubtitles(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (const auto colon = line.find(':'); colon != std::string::npos) {
            out.push_back(line.substr(colon + 1));
        }
    }
    return out;
}

} // namespace geck
