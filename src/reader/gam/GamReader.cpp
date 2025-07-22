#include "GamReader.h"

#include <iostream>
#include <regex>
#include <spdlog/spdlog.h>

#include "../../format/gam/Gam.h"

namespace geck {

std::unique_ptr<Gam> GamReader::read() {
    auto gam = std::make_unique<Gam>(_path);

    const std::regex regex_key_value(R"~(^\s*(\w+)\s*:=(\d+)\s*;)~");
    const std::regex regex_gvars_start(R"~(^\s*GAME_GLOBAL_VARS:)~"); // GVARS
    const std::regex regex_mvars_start(R"~(^\s*MAP_GLOBAL_VARS:)~");  // MVARS

    std::smatch regex_match;

    bool parsingMvars = false;
    bool parsingGvars = false;

    std::string contents = _stream.readString(_stream.size());
    std::stringstream stream(contents);
    for (std::string line; std::getline(stream, line);) {

        // GAME_GLOBAL_VARS section start
        if (std::regex_search(line, regex_gvars_start)) {
            parsingGvars = true;
            parsingMvars = false;
            continue;
        }

        // MAP_GLOBAL_VARS section start
        if (std::regex_search(line, regex_mvars_start)) {
            parsingGvars = false;
            parsingMvars = true;
            continue;
        }

        if (std::regex_search(line, regex_match, regex_key_value)) {
            // The first match is the whole string
            if (regex_match.size() == 3) {
                auto key = regex_match[1].str();
                auto value = regex_match[2].str();

                if (parsingGvars) {
                    gam->addGvar(key, std::stoi(value));
                } else if (parsingMvars) {
                    gam->addMvar(key, std::stoi(value));
                } else {
                    throw std::runtime_error{ "Variable " + key + " does not belong to either MVARS nor GVARS section" };
                }
            }
        }
    }

    return gam;
}

} // namespace geck
