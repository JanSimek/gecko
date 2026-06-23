#include "LstReader.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <spdlog/spdlog.h>

#include "format/lst/Lst.h"
#include "reader/ErrorMessages.h"

namespace geck {

std::string parseLine(std::string line) {
    // strip a trailing or full-line comment (everything from the first ';').
    // Note: find() returning 0 (a leading ';') must still strip, so test against
    // npos explicitly rather than relying on the position being truthy.
    if (auto pos = line.find(';'); pos != std::string::npos) {
        line = line.substr(0, pos);
    }

    // rtrim
    line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(),
        line.end());

    // normalize backslash paths to forward slashes
    std::replace(line.begin(), line.end(), '\\', '/');

    std::transform(line.begin(), line.end(), line.begin(), ::tolower);

    return line;
}

// The human-readable comment after ';' on an LST line (e.g. a script's description in scripts.lst), with
// any trailing "# ..." metadata removed (scripts.lst appends "# local_vars=N") and both ends trimmed.
// Empty when the line has no ';'. Case is preserved, unlike the lowercased entry name.
std::string parseComment(const std::string& line) {
    const auto semi = line.find(';');
    if (semi == std::string::npos) {
        return {};
    }
    std::string comment = line.substr(semi + 1);
    if (const auto hash = comment.find('#'); hash != std::string::npos) {
        comment.erase(hash);
    }
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    comment.erase(comment.begin(), std::find_if(comment.begin(), comment.end(), notSpace));
    comment.erase(std::find_if(comment.rbegin(), comment.rend(), notSpace).base(), comment.end());
    return comment;
}

std::unique_ptr<Lst> LstReader::read() {
    try {
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading LST file: {}", _path.string());

        auto pos = utils.getPosition();
        spdlog::trace("LST file size: {} bytes", pos.total);

        // LST files can be empty, but validate format if non-empty
        if (pos.total > 0) {
            FormatValidator::validateLstFile(utils, _path);
        }

        std::vector<std::string> list;
        std::vector<std::string> comments;
        int lines_processed = 0;
        int valid_entries = 0;

        if (pos.total == 0) {
            spdlog::debug("Empty LST file");
        } else {
            std::string contents = utils.readFixedString(pos.total);

            std::istringstream stream(contents);
            std::string line;

            while (std::getline(stream, line)) {
                lines_processed++;

                // Handle different line ending types (\r\n, \n, \r)
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                std::string parsed_line = parseLine(line);

                if (!parsed_line.empty()) {
                    list.push_back(parsed_line);
                    comments.push_back(parseComment(line)); // line is the raw text; parseLine took a copy
                    valid_entries++;

                    spdlog::trace("LST entry {}: '{}'", valid_entries, parsed_line);
                }
            }
        }

        spdlog::debug("Successfully read LST file: {} lines processed, {} valid entries",
            lines_processed, valid_entries);

        auto lst = std::make_unique<Lst>(_path);
        lst->setList(list);
        lst->setComments(comments);
        return lst;

    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException(
            "Failed to parse LST file: " + std::string(e.what()), _path);
    }
}

} // namespace geck