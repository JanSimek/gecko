#include "reader/quests/QuestsTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>
#include <string>
#include <vector>

namespace geck {

namespace {

    using geck::text::intOr;
    using geck::text::trim;

    // Split on any of space/tab/comma (the engine's questInit delimiters), dropping empty tokens.
    std::vector<std::string> tokenize(const std::string& s) {
        std::vector<std::string> out;
        std::string current;
        for (const char c : s) {
            if (c == ' ' || c == '\t' || c == ',') {
                if (!current.empty()) {
                    out.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            out.push_back(current);
        }
        return out;
    }

} // namespace

QuestsTxt parseQuestsTxt(std::istream& in) {
    QuestsTxt out;
    std::string line;
    while (std::getline(in, line)) {
        const std::string content = trim(line.substr(0, line.find('#'))); // '#' starts a comment
        if (content.empty()) {
            continue;
        }
        const auto tokens = tokenize(content);
        if (tokens.size() < 5) {
            continue; // not a quest row
        }
        Quest quest;
        quest.location = intOr(tokens[0], -1);
        quest.description = intOr(tokens[1], -1);
        quest.gvar = intOr(tokens[2], -1);
        quest.displayThreshold = intOr(tokens[3], 0);
        quest.completedThreshold = intOr(tokens[4], 0);
        out.quests.push_back(quest);
    }
    return out;
}

QuestsTxt parseQuestsTxt(const std::string& text) {
    std::istringstream in(text);
    return parseQuestsTxt(in);
}

} // namespace geck
