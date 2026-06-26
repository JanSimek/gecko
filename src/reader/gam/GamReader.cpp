#include "GamReader.h"

#include "format/gam/Gam.h"
#include "reader/ErrorMessages.h"
#include "reader/TextParsing.h"

#include <regex>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace geck {

namespace {

    using geck::text::splitLines;
    using geck::text::trim;

    // A value may be negative (e.g. GVAR_*_PRICE := -1), so accept a leading '-'. Capture group 2 is the
    // integer value; its span (match.position(2)/match.length(2)) splits the line into the prefix (up to
    // the integer) and suffix (after it) so an edit rebuilds the line from those parts instead of a
    // positional substring replace.
    const std::regex kVariable(R"~(^\s*(\w+)\s*:=\s*(-?\d+)\s*;)~");
    const std::regex kGvarsStart(R"~(^\s*GAME_GLOBAL_VARS:)~");
    const std::regex kMvarsStart(R"~(^\s*MAP_GLOBAL_VARS:)~");
    const std::regex kComment(R"~(^\s*//)~");

    GamLine classifyLine(const std::string& raw, GamLine::Section section) {
        GamLine line;
        line.raw = raw;
        line.section = section;

        if (trim(raw).empty()) {
            line.kind = GamLine::Kind::Blank;
            return line;
        }
        if (std::regex_search(raw, kComment)) {
            line.kind = GamLine::Kind::Comment;
            return line;
        }
        if (std::regex_search(raw, kGvarsStart) || std::regex_search(raw, kMvarsStart)) {
            line.kind = GamLine::Kind::SectionHeader;
            return line;
        }
        if (std::smatch match; std::regex_search(raw, match, kVariable)) {
            line.kind = GamLine::Kind::Variable;
            line.name = match[1].str();
            line.value = std::stoi(match[2].str());
            const auto valuePos = static_cast<std::size_t>(match.position(2));
            const auto valueLen = static_cast<std::size_t>(match.length(2));
            line.valuePrefix = raw.substr(0, valuePos);
            line.valueSuffix = raw.substr(valuePos + valueLen);
            return line;
        }
        line.kind = GamLine::Kind::Other;
        return line;
    }

} // namespace

Gam GamReader::parse(const std::string& content) {
    Gam gam;
    bool finalNewline = true;
    const std::vector<std::string> rawLines = splitLines(content, finalNewline);
    gam.finalNewline = finalNewline;

    GamLine::Section section = GamLine::Section::None;
    for (const std::string& raw : rawLines) {
        // A section header takes effect on its own line and for every line after it (the header line is
        // recorded with the section it opens, matching how mapGlobalVars() filters on section).
        if (std::regex_search(raw, kGvarsStart)) {
            section = GamLine::Section::GameGlobalVars;
        } else if (std::regex_search(raw, kMvarsStart)) {
            section = GamLine::Section::MapGlobalVars;
        }
        gam.lines.push_back(classifyLine(raw, section));
    }
    return gam;
}

std::unique_ptr<Gam> GamReader::read() {
    auto& utils = getBinaryUtils();
    spdlog::debug("Reading GAM file: {}", _path.string());

    const auto pos = utils.getPosition();
    if (pos.total == 0) {
        throw UnsupportedFormatException("Empty GAM file", _path);
    }

    const std::string contents = utils.readFixedString(pos.total);
    if (contents.find("GAME_GLOBAL_VARS:") == std::string::npos
        && contents.find("MAP_GLOBAL_VARS:") == std::string::npos) {
        throw UnsupportedFormatException(
            ErrorMessages::corruptedData(_path, "Missing required GAM sections"), _path);
    }

    Gam parsed = parse(contents);
    auto gam = std::make_unique<Gam>(_path);
    gam->lines = std::move(parsed.lines);
    gam->finalNewline = parsed.finalNewline;

    // A variable that precedes any section header is malformed (its ordinal would be undefined). The
    // lossless parser keeps it as a None-section Variable line, so reject the file if one exists.
    for (const GamLine& line : gam->lines) {
        if (line.kind == GamLine::Kind::Variable && line.section == GamLine::Section::None) {
            throw ParseException(
                ErrorMessages::corruptedData(_path, "Variable '" + line.name + "' outside of GVARS/MVARS section"),
                _path);
        }
    }

    spdlog::debug("Successfully read GAM file: {} gvars, {} mvars", gam->gvarCount(), gam->mvarCount());
    return gam;
}

} // namespace geck
