#include "writer/maps/MapsTxtValidator.h"

#include "reader/TextParsing.h"

#include <algorithm>
#include <set>
#include <string>

namespace geck::writer {

namespace {

    using geck::text::splitCsv;
    using geck::text::toLower;
    using geck::text::trim;
    using Severity = MapsTxtIssue::Severity;

    // The set of keys the engine/editor recognises; anything else is reported as Info (passthrough).
    bool isKnownKey(const std::string& key) {
        static const std::set<std::string> known = { "lookup_name", "map_name", "music", "ambient_sfx",
            "saved", "dead_bodies_age", "pipboy_active", "automap", "can_rest_here", "state",
            "random_start_point", "destroyed_maps_as", "destroyed_maps_on_var" };
        // random_start_point_N -> compare the prefix before the trailing _N
        const auto us = key.find_last_of('_');
        if (us != std::string::npos && key.rfind("random_start_point", 0) == 0) {
            return true;
        }
        return known.count(key) != 0;
    }

    const MapsTxtLine* findKey(const MapsTxtSection& section, const std::string& key) {
        for (const MapsTxtLine& line : section.lines) {
            if (line.kind == MapsTxtLine::Kind::KeyValue && toLower(trim(line.key)) == key) {
                return &line;
            }
        }
        return nullptr;
    }

    void validateSection(const MapsTxtSection& section, std::vector<MapsTxtIssue>& issues) {
        if (findKey(section, "lookup_name") == nullptr) {
            issues.push_back({ Severity::Error, section.index, "missing required key 'lookup_name'" });
        }
        if (findKey(section, "map_name") == nullptr) {
            issues.push_back({ Severity::Error, section.index, "missing required key 'map_name'" });
        }
        if (const MapsTxtLine* sfx = findKey(section, "ambient_sfx"); sfx != nullptr && splitCsv(sfx->value).size() > 7) {
            issues.push_back({ Severity::Error, section.index, "ambient_sfx has more than 7 entries (engine cap)" });
        }
        if (const MapsTxtLine* rest = findKey(section, "can_rest_here"); rest != nullptr && splitCsv(rest->value).size() != 3) {
            issues.push_back({ Severity::Error, section.index, "can_rest_here must list exactly 3 values (one per elevation)" });
        }
        for (const MapsTxtLine& line : section.lines) {
            if (line.kind == MapsTxtLine::Kind::KeyValue && !isKnownKey(toLower(trim(line.key)))) {
                issues.push_back({ Severity::Info, section.index, "unrecognised key '" + trim(line.key) + "'" });
            }
        }
    }

} // namespace

std::vector<MapsTxtIssue> validateMapsTxt(const MapsTxt& doc) {
    std::vector<MapsTxtIssue> issues;

    std::set<int> seen;
    int maxIndex = -1;
    for (const MapsTxtSection& section : doc.sections) {
        if (section.index < 0) {
            continue; // a non-[Map N] section is passed through untouched
        }
        if (!seen.insert(section.index).second) {
            issues.push_back({ Severity::Error, section.index, "duplicate [Map " + std::to_string(section.index) + "] section" });
        }
        maxIndex = std::max(maxIndex, section.index);
        validateSection(section, issues);
    }

    // The engine reads [Map 0] upward and stops at the first missing index, so a gap silently drops
    // every later map -> Error.
    for (int i = 0; i < maxIndex; ++i) {
        if (seen.count(i) == 0) {
            issues.push_back({ Severity::Error, i, "missing [Map " + std::to_string(i) + "] (the sequence must have no gaps)" });
        }
    }

    return issues;
}

bool hasErrors(const std::vector<MapsTxtIssue>& issues) {
    return std::any_of(issues.begin(), issues.end(),
        [](const MapsTxtIssue& issue) { return issue.severity == Severity::Error; });
}

} // namespace geck::writer
