#include "writer/maps/MapsTxtSerializer.h"

#include "reader/TextParsing.h"

#include <cstddef>
#include <vector>

namespace geck::writer {

namespace {
    using geck::text::toLower;
    using geck::text::trim;

    bool keyMatches(const MapsTxtLine& line, const std::string& key) {
        return line.kind == MapsTxtLine::Kind::KeyValue && toLower(trim(line.key)) == toLower(key);
    }
} // namespace

std::string serializeMapsTxt(const MapsTxtDocument& doc) {
    std::vector<const std::string*> raws;
    for (const MapsTxtLine& line : doc.preamble) {
        raws.push_back(&line.raw);
    }
    for (const MapsTxtSection& section : doc.sections) {
        raws.push_back(&section.headerRaw);
        for (const MapsTxtLine& line : section.lines) {
            raws.push_back(&line.raw);
        }
    }

    std::string out;
    for (std::size_t i = 0; i < raws.size(); ++i) {
        out += *raws[i];
        if (i + 1 < raws.size() || doc.finalNewline) {
            out += '\n';
        }
    }
    return out;
}

std::optional<std::string> findField(const MapsTxtDocument& doc, int sectionIndex, const std::string& key) {
    const MapsTxtSection* section = doc.section(sectionIndex);
    if (section == nullptr) {
        return std::nullopt;
    }
    for (const MapsTxtLine& line : section->lines) {
        if (keyMatches(line, key)) {
            return line.value;
        }
    }
    return std::nullopt;
}

bool setField(MapsTxtDocument& doc, int sectionIndex, const std::string& key, const std::string& value) {
    MapsTxtSection* section = doc.section(sectionIndex);
    if (section == nullptr) {
        return false;
    }
    for (MapsTxtLine& line : section->lines) {
        if (keyMatches(line, key)) {
            line.value = value;
            line.raw = line.key + "=" + value + line.inlineComment; // rebuild only this line
            return true;
        }
    }
    return false; // key not present in the section
}

} // namespace geck::writer
