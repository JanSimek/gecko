#include "format/maps/MapsTxt.h"

#include "reader/TextParsing.h"

#include <string>

namespace geck {

namespace {

    using geck::text::toLower;
    using geck::text::trim;

    // The value of `key` (lowercase) in a section, or "" — keys may carry original case/spacing.
    std::string fieldValue(const MapsTxtSection& section, const std::string& key) {
        for (const MapsTxtLine& line : section.lines) {
            if (line.kind == MapsTxtLine::Kind::KeyValue && toLower(trim(line.key)) == key) {
                return line.value; // already trimmed (see MapsTxtLine)
            }
        }
        return {};
    }

    // maps.txt stores the bare name ("arcaves"); the engine lowercases it and appends ".map" when
    // loading. Normalize to that loadable filename so it matches a map path's basename.
    std::string normalizedMapName(const std::string& raw) {
        std::string name = toLower(raw);
        if (!name.empty() && name.find('.') == std::string::npos) {
            name += ".map";
        }
        return name;
    }

    MapInfo toMapInfo(const MapsTxtSection& section) {
        return MapInfo{ section.index, fieldValue(section, "lookup_name"), normalizedMapName(fieldValue(section, "map_name")) };
    }

} // namespace

std::optional<MapInfo> MapsTxt::find(int index) const {
    const MapsTxtSection* sec = section(index);
    return sec != nullptr ? std::optional<MapInfo>{ toMapInfo(*sec) } : std::nullopt;
}

std::optional<MapInfo> MapsTxt::findByName(const std::string& mapFileName) const {
    const std::string target = normalizedMapName(mapFileName);
    for (const MapsTxtSection& sec : sections) {
        if (sec.index >= 0 && normalizedMapName(fieldValue(sec, "map_name")) == target) {
            return toMapInfo(sec);
        }
    }
    return std::nullopt;
}

std::optional<MapInfo> MapsTxt::findByLookupName(const std::string& lookupName) const {
    const std::string target = toLower(lookupName);
    for (const MapsTxtSection& sec : sections) {
        if (sec.index >= 0 && toLower(fieldValue(sec, "lookup_name")) == target) {
            return toMapInfo(sec);
        }
    }
    return std::nullopt;
}

} // namespace geck
