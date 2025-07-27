#include "GamReader.h"

#include <iostream>
#include <regex>
#include <spdlog/spdlog.h>

#include "../../format/gam/Gam.h"
#include "../ErrorMessages.h"

namespace geck {

std::unique_ptr<Gam> GamReader::read() {
    try {
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading GAM file: {}", _path.string());
        
        // Validate file size
        auto pos = utils.getPosition();
        if (pos.total == 0) {
            throw UnsupportedFormatException("Empty GAM file", _path);
        }
        
        spdlog::trace("GAM file size: {} bytes", pos.total);
        
        auto gam = std::make_unique<Gam>(_path);

        // Define regex patterns for GAM file parsing
        const std::regex regex_key_value(R"~(^\s*(\w+)\s*:=\s*(\d+)\s*;)~"); // More flexible whitespace
        const std::regex regex_gvars_start(R"~(^\s*GAME_GLOBAL_VARS:)~"); // GVARS
        const std::regex regex_mvars_start(R"~(^\s*MAP_GLOBAL_VARS:)~");  // MVARS
        const std::regex regex_comment(R"~(^\s*//.*$)~"); // Comment lines

        std::smatch regex_match;

        bool parsingMvars = false;
        bool parsingGvars = false;
        int lines_processed = 0;
        int variables_found = 0;

        // Read entire file content as string using BinaryUtils
        std::string contents = utils.readFixedString(pos.total);
        
        // Validate that the content contains expected sections
        if (contents.find("GAME_GLOBAL_VARS:") == std::string::npos &&
            contents.find("MAP_GLOBAL_VARS:") == std::string::npos) {
            throw UnsupportedFormatException(
                ErrorMessages::corruptedData(_path, "Missing required GAM sections"), _path);
        }
        // Parse the content line by line
        std::stringstream stream(contents);
        for (std::string line; std::getline(stream, line);) {
            lines_processed++;
            
            // Skip comment lines
            if (std::regex_search(line, regex_comment)) {
                spdlog::trace("Skipping comment line {}: {}", lines_processed, line);
                continue;
            }
            
            // Skip empty lines
            if (line.empty() || std::regex_match(line, std::regex(R"~(^\s*$)~"))) {
                continue;
            }
            
            // GAME_GLOBAL_VARS section start
            if (std::regex_search(line, regex_gvars_start)) {
                parsingGvars = true;
                parsingMvars = false;
                spdlog::trace("Found GAME_GLOBAL_VARS section at line {}", lines_processed);
                continue;
            }

            // MAP_GLOBAL_VARS section start
            if (std::regex_search(line, regex_mvars_start)) {
                parsingGvars = false;
                parsingMvars = true;
                spdlog::trace("Found MAP_GLOBAL_VARS section at line {}", lines_processed);
                continue;
            }

            // Parse variable assignment
            if (std::regex_search(line, regex_match, regex_key_value)) {
                if (regex_match.size() == 3) {
                    auto key = regex_match[1].str();
                    auto value_str = regex_match[2].str();
                    
                    try {
                        int value = std::stoi(value_str);
                        variables_found++;
                        
                        if (parsingGvars) {
                            gam->addGvar(key, value);
                            spdlog::trace("Added GVAR: {} = {}", key, value);
                        } else if (parsingMvars) {
                            gam->addMvar(key, value);
                            spdlog::trace("Added MVAR: {} = {}", key, value);
                        } else {
                            throw ParseException(
                                ErrorMessages::corruptedData(_path, 
                                    "Variable " + key + " outside of GVARS/MVARS section at line " + std::to_string(lines_processed) + ": " + line), _path);
                        }
                    } catch (const std::invalid_argument&) {
                        throw ParseException(
                            ErrorMessages::corruptedData(_path,
                                "Invalid variable value '" + value_str + "' for " + key + " at line " + std::to_string(lines_processed) + ": " + line), _path);
                    }
                }
            } else if (!line.empty()) {
                // Log non-matching lines for debugging (but don't treat as error)
                spdlog::trace("Line {} doesn't match any pattern: '{}'", lines_processed, line);
            }
        }
        
        spdlog::debug("Successfully read GAM file: {} lines processed, {} variables found", 
                     lines_processed, variables_found);

        return gam;
        
    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException(
            "Failed to parse GAM file: " + std::string(e.what()), _path);
    }
}

} // namespace geck
