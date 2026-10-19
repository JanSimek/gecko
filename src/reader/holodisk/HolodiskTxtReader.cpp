#include "reader/holodisk/HolodiskTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>
#include <string>

namespace geck {

namespace {
    using geck::text::intOr;
    using geck::text::splitTokens;
    using geck::text::trim;
} // namespace

HolodiskTxt parseHolodiskTxt(const std::string& text) {
    std::istringstream in(text);
    HolodiskTxt out;
    std::string line;
    while (std::getline(in, line)) {
        const std::string content = trim(line.substr(0, line.find('#'))); // '#' starts a comment
        if (content.empty()) {
            continue;
        }
        const auto tokens = splitTokens(content); // gvar, title, text
        if (tokens.size() < 3) {
            continue;
        }
        out.holodisks.push_back({ intOr(tokens[0], -1), intOr(tokens[1], -1), intOr(tokens[2], -1) });
    }
    return out;
}

} // namespace geck
