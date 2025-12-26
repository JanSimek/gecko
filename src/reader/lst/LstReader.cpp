#include "LstReader.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <spdlog/spdlog.h>

#include "../../format/lst/Lst.h"
#include "../ErrorMessages.h"

namespace geck {

std::string parseLine(std::string line) {
    // strip comments
    if (auto pos = line.find(';')) {
        line = line.substr(0, pos);
    }

    // rtrim
    line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(),
        line.end());

    // replace slashes
    std::replace(line.begin(), line.end(), '\\', '/');

    // to lower
    std::transform(line.begin(), line.end(), line.begin(), ::tolower);

    return line;
}

std::unique_ptr<Lst> LstReader::read() {
    try {
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading LST file: {}", _path.string());

        // Validate file size
        auto pos = utils.getPosition();
        spdlog::trace("LST file size: {} bytes", pos.total);

        // LST files can be empty, but validate format if non-empty
        if (pos.total > 0) {
            FormatValidator::validateLstFile(utils, _path);
        }

        std::vector<std::string> list;
        int lines_processed = 0;
        int valid_entries = 0;

        if (pos.total == 0) {
            spdlog::debug("Empty LST file");
        } else {
            // Read entire file content as string using BinaryUtils
            std::string contents = utils.readFixedString(pos.total);

            // Parse content line by line with better line ending support
            std::istringstream stream(contents);
            std::string line;

            while (std::getline(stream, line)) {
                lines_processed++;

                // Handle different line ending types (\r\n, \n, \r)
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                // Parse and clean up the line
                std::string parsed_line = parseLine(line);

                // Skip empty lines after parsing
                if (!parsed_line.empty()) {
                    list.push_back(parsed_line);
                    valid_entries++;

                    spdlog::trace("LST entry {}: '{}'", valid_entries, parsed_line);
                }
            }
        }

        spdlog::debug("Successfully read LST file: {} lines processed, {} valid entries",
            lines_processed, valid_entries);

        auto lst = std::make_unique<Lst>(_path);
        lst->setList(list);
        return lst;

    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException(
            "Failed to parse LST file: " + std::string(e.what()), _path);
    }
}

} // namespace geck