#pragma once

#include <functional>
#include <istream>
#include <string>

#include "reader/TextParsing.h"

namespace geck::ini {

/// Drive the line loop shared by Fallout's `[Section]` / `key=value` .txt configs (maps.txt,
/// city.txt, worldmap.txt, ai.txt): strip inline `;` comments, skip blanks, and for each line call
/// either @p onSection (with the trimmed text inside `[...]`, case preserved) or @p onField (with the
/// **lowercased** key and the trimmed value). Keys before the first section are ignored. The caller
/// supplies the section/field semantics (flush-and-start, which keys it cares about) in the callbacks,
/// so each reader keeps only its own logic and not a copy of the loop.
inline void parse(std::istream& in,
    const std::function<void(const std::string& section)>& onSection,
    const std::function<void(const std::string& key, const std::string& value)>& onField) {
    bool inSection = false;
    std::string line;
    while (std::getline(in, line)) {
        const std::string content = text::trim(text::stripComment(line));
        if (content.empty()) {
            continue;
        }
        if (content.front() == '[' && content.back() == ']') {
            onSection(text::trim(content.substr(1, content.size() - 2)));
            inSection = true;
            continue;
        }
        if (!inSection) {
            continue; // a stray key before the first section
        }
        const auto eq = content.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        onField(text::toLower(text::trim(content.substr(0, eq))), text::trim(content.substr(eq + 1)));
    }
}

} // namespace geck::ini
