#include "writer/maps/MapsTxtSerializer.h"

#include "reader/TextParsing.h"

#include <algorithm>
#include <string>
#include <utility>
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
    std::vector<std::string> raws;
    for (const MapsTxtLine& line : doc.preamble) {
        raws.push_back(line.raw);
    }
    for (const MapsTxtSection& section : doc.sections) {
        raws.push_back(section.headerRaw);
        for (const MapsTxtLine& line : section.lines) {
            raws.push_back(line.raw);
        }
    }
    return geck::text::joinLinesLf(raws, doc.finalNewline);
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

namespace {
    MapsTxtLine keyValueLine(const std::string& key, const std::string& value) {
        MapsTxtLine line;
        line.kind = MapsTxtLine::Kind::KeyValue;
        line.key = key;
        line.value = value;
        line.raw = key + "=" + value;
        return line;
    }

    std::string zeroPadded(int index) {
        std::string num = std::to_string(index);
        while (num.size() < 3) { // shipped headers are 3-digit ([Map 000])
            num.insert(num.begin(), '0');
        }
        return num;
    }
} // namespace

bool addSection(MapsTxtDocument& doc, int index, const std::string& lookupName, const std::string& mapName) {
    if (index < 0 || doc.section(index) != nullptr) {
        return false;
    }
    MapsTxtSection section;
    section.index = index;
    section.headerRaw = "[Map " + zeroPadded(index) + "]";
    section.lines.push_back(keyValueLine("lookup_name", lookupName));
    section.lines.push_back(keyValueLine("map_name", mapName));

    // Insert in index order so the file stays ordered.
    auto pos = std::find_if(doc.sections.begin(), doc.sections.end(),
        [index](const MapsTxtSection& s) { return s.index > index; });
    doc.sections.insert(pos, std::move(section));
    return true;
}

bool removeSection(MapsTxtDocument& doc, int index) {
    const auto pos = std::find_if(doc.sections.begin(), doc.sections.end(),
        [index](const MapsTxtSection& s) { return s.index == index; });
    if (pos == doc.sections.end()) {
        return false;
    }
    doc.sections.erase(pos);
    return true;
}

} // namespace geck::writer
