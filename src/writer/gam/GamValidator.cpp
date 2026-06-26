#include "writer/gam/GamValidator.h"

#include "reader/gam/GamReader.h"
#include "writer/gam/GamSerializer.h"

#include <algorithm>
#include <regex>
#include <set>
#include <string>

namespace geck::writer {

namespace {

    using Severity = GamIssue::Severity;

    // The engine's variable-line shape; an edited line must still match this for the engine to parse it.
    const std::regex kVariableShape(R"~(^\s*(\w+)\s*:=\s*(-?\d+)\s*;)~");

    // Round-trip integrity: a written document must re-parse to the same map-global variables (same
    // count, names, and values). A divergence means an edit broke the structure -> blocking Error. This
    // is the single check that makes editing trustworthy; it reuses the real serialiser/reader rather
    // than re-deriving the values, so it cannot drift from the production write path.
    void checkRoundTrip(const Gam& doc, std::vector<GamIssue>& issues) {
        const std::vector<std::pair<std::string, int>> before = doc.mapGlobalVars();
        const Gam reparsed = GamReader::parse(serializeGam(doc));
        const std::vector<std::pair<std::string, int>> after = reparsed.mapGlobalVars();

        if (before.size() != after.size()) {
            issues.push_back({ Severity::Error,
                "serialising and re-reading changed the MAP_GLOBAL_VARS count ("
                    + std::to_string(before.size()) + " -> " + std::to_string(after.size())
                    + "): an edit corrupted the .gam structure" });
            return; // index-wise comparison below is meaningless once counts differ
        }
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (before[i] != after[i]) {
                issues.push_back({ Severity::Error,
                    "MAP_GLOBAL_VARS variable '" + before[i].first
                        + "' did not survive a serialise/re-read round trip: an edit corrupted the .gam structure" });
            }
        }
    }

    // Value sanity: each edited variable's value must to_string round-trip back to the same int, and its
    // rebuilt line must still match the engine's variable shape.
    void checkValueSanity(const Gam& doc, std::vector<GamIssue>& issues) {
        for (const GamLine& line : doc.lines) {
            if (line.kind != GamLine::Kind::Variable || line.section != GamLine::Section::MapGlobalVars) {
                continue;
            }
            try {
                if (std::stoi(std::to_string(line.value)) != line.value) {
                    issues.push_back({ Severity::Error,
                        "MAP_GLOBAL_VARS variable '" + line.name + "' has a value that does not round-trip" });
                }
            } catch (const std::exception&) {
                issues.push_back({ Severity::Error,
                    "MAP_GLOBAL_VARS variable '" + line.name + "' has an unparseable value" });
            }
            if (!std::regex_search(line.raw, kVariableShape)) {
                issues.push_back({ Severity::Error,
                    "MAP_GLOBAL_VARS variable '" + line.name
                        + "' no longer matches the engine's 'NAME := value;' shape" });
            }
        }
    }

    // Structure: surface a missing section header (Warning) and duplicate variable names (Info).
    void checkStructure(const Gam& doc, std::vector<GamIssue>& issues) {
        bool hasMapVarsHeader = false;
        bool hasMapVars = false;
        for (const GamLine& line : doc.lines) {
            if (line.kind == GamLine::Kind::SectionHeader && line.section == GamLine::Section::MapGlobalVars) {
                hasMapVarsHeader = true;
            }
            if (line.kind == GamLine::Kind::Variable && line.section == GamLine::Section::MapGlobalVars) {
                hasMapVars = true;
            }
        }
        if (hasMapVars && !hasMapVarsHeader) {
            issues.push_back({ Severity::Warning,
                "document has MAP_GLOBAL_VARS variables but no 'MAP_GLOBAL_VARS:' section header" });
        }

        std::set<std::string> seen;
        for (const auto& [name, value] : doc.mapGlobalVars()) {
            (void)value;
            if (!seen.insert(name).second) {
                issues.push_back({ Severity::Info,
                    "duplicate MAP_GLOBAL_VARS variable name '" + name
                        + "' (the engine indexes positionally and ignores names)" });
            }
        }
    }

} // namespace

std::vector<GamIssue> validateGam(const Gam& doc) {
    std::vector<GamIssue> issues;
    checkRoundTrip(doc, issues);
    checkValueSanity(doc, issues);
    checkStructure(doc, issues);
    return issues;
}

bool hasErrors(const std::vector<GamIssue>& issues) {
    return std::any_of(issues.begin(), issues.end(),
        [](const GamIssue& issue) { return issue.severity == Severity::Error; });
}

} // namespace geck::writer
