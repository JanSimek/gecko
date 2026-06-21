#include "reader/endgame/EndgameTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>
#include <string>

namespace geck {

namespace {
    using geck::text::intOr;
    using geck::text::splitCsv;
    using geck::text::trim;
} // namespace

EndgameTxt parseEndgameTxt(std::istream& in) {
    EndgameTxt out;
    std::string line;
    while (std::getline(in, line)) {
        const std::string content = trim(line.substr(0, line.find('#'))); // '#' starts a comment
        if (content.empty()) {
            continue;
        }
        const auto fields = splitCsv(content); // gvar, value, art, narrator [, direction]
        if (fields.size() < 4) {
            continue; // not a slide row
        }
        Ending ending;
        ending.gvar = intOr(fields[0], -1);
        ending.value = intOr(fields[1], 0);
        ending.art = intOr(fields[2], -1);
        ending.narrator = fields[3];
        if (fields.size() > 4) {
            ending.direction = intOr(fields[4], 1); // engine default is 1 (endgame.cc)
        }
        out.endings.push_back(ending);
    }
    return out;
}

EndgameTxt parseEndgameTxt(const std::string& text) {
    std::istringstream in(text);
    return parseEndgameTxt(in);
}

} // namespace geck
