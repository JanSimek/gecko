#include "reader/karma/KarmaVarTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>
#include <string>

namespace geck {

namespace {
    using geck::text::intOr;
    using geck::text::splitTokens;
    using geck::text::trim;
} // namespace

KarmaVarTxt parseKarmaVarTxt(const std::string& text) {
    std::istringstream in(text);
    KarmaVarTxt out;
    std::string line;
    while (std::getline(in, line)) {
        const std::string content = trim(line.substr(0, line.find('#'))); // '#' starts a comment
        if (content.empty()) {
            continue;
        }
        const auto tokens = splitTokens(content); // gvar, art, name, description
        if (tokens.size() < 4) {
            continue;
        }
        out.entries.push_back({ intOr(tokens[0], -1), intOr(tokens[1], -1), intOr(tokens[2], -1),
            intOr(tokens[3], -1) });
    }
    return out;
}

} // namespace geck
