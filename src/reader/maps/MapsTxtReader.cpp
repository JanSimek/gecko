#include "reader/maps/MapsTxtReader.h"

#include "reader/TextParsing.h"

#include <cstddef>
#include <vector>

namespace geck {

namespace {

    using geck::text::splitLines;
    using geck::text::toLower;
    using geck::text::trim;

    // "Map NNN" (the text inside the brackets) -> NNN, or -1; mirrors MapsTxtReader::parseSectionIndex.
    int sectionIndex(const std::string& inner) {
        if (toLower(inner).rfind("map", 0) != 0) {
            return -1;
        }
        try {
            return std::stoi(inner.substr(3));
        } catch (const std::exception&) {
            return -1;
        }
    }

    MapsTxtLine classifyLine(const std::string& raw) {
        MapsTxtLine line;
        line.raw = raw;
        const std::string trimmed = trim(raw);
        if (trimmed.empty()) {
            line.kind = MapsTxtLine::Kind::Blank;
            return line;
        }
        if (trimmed.front() == ';') { // a comment or a commented-out ;key=value
            line.kind = MapsTxtLine::Kind::Comment;
            return line;
        }
        const std::size_t eq = raw.find('=');
        if (eq == std::string::npos) {
            line.kind = MapsTxtLine::Kind::Comment; // not a key=value; keep verbatim via raw
            return line;
        }
        line.kind = MapsTxtLine::Kind::KeyValue;
        line.key = raw.substr(0, eq);
        const std::string after = raw.substr(eq + 1);
        const std::size_t semi = after.find(';');
        if (semi == std::string::npos) {
            line.value = trim(after);
            return line;
        }
        // Split value from the inline "; comment", keeping the comment's leading whitespace with it so a
        // later edit can reproduce the suffix exactly (maps.txt has no whitespace before the value).
        line.value = trim(after.substr(0, semi));
        const std::size_t valuePos = after.find(line.value);
        line.inlineComment = (line.value.empty() || valuePos == std::string::npos)
            ? after
            : after.substr(valuePos + line.value.size());
        return line;
    }

} // namespace

MapsTxt parseMapsTxt(const std::string& content) {
    MapsTxt doc;
    bool finalNewline = true;
    const std::vector<std::string> rawLines = splitLines(content, finalNewline);
    doc.finalNewline = finalNewline;

    MapsTxtSection* current = nullptr;
    for (const std::string& raw : rawLines) {
        const std::string trimmed = trim(raw);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            MapsTxtSection section;
            section.headerRaw = raw;
            section.index = sectionIndex(trim(trimmed.substr(1, trimmed.size() - 2)));
            doc.sections.push_back(std::move(section));
            current = &doc.sections.back();
            continue;
        }
        MapsTxtLine line = classifyLine(raw);
        if (current != nullptr) {
            current->lines.push_back(std::move(line));
        } else {
            doc.preamble.push_back(std::move(line));
        }
    }
    return doc;
}

} // namespace geck
