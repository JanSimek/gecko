#include "cli/ScriptLookup.h"

#include "format/lst/Lst.h"
#include "reader/TextParsing.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"

#include <spdlog/spdlog.h>

#include <ostream>

namespace geck::cli::script_lookup {

using text::toLower;

std::string stem(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

std::unordered_map<std::string, std::string> indexSslSources(resource::GameResources& resources) {
    std::unordered_map<std::string, std::string> index;
    for (const auto& path : resources.files().list("*")) {
        if (text::toLower(path.extension().string()) == ".ssl") {
            index.try_emplace(toLower(stem(path.generic_string())), path.generic_string());
        }
    }
    return index;
}

const Lst* loadScriptsLst(resource::GameResources& resources) {
    try {
        return resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
    } catch (const std::exception& e) {
        spdlog::debug("scripts.lst load failed: {}", e.what());
        return nullptr;
    }
}

nlohmann::ordered_json scriptIdentity(resource::GameResources& resources, const Lst& scripts, int programIndex) {
    const std::string& filename = scripts.list()[static_cast<std::size_t>(programIndex)];
    nlohmann::ordered_json identity;
    identity["programIndex"] = programIndex;
    identity["sslConstant"] = programIndex + 1;
    identity["filename"] = filename;
    identity["name"] = stem(filename);
    const std::string description = resource::scriptDescription(resources, programIndex);
    identity["description"] = description.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(description);
    return identity;
}

Resolution resolveScript(const Lst& scripts, const std::string& name, int programIndex) {
    const auto& list = scripts.list();
    if (!name.empty()) {
        const std::string needle = toLower(stem(name));
        std::vector<int> exact;
        std::vector<int> partial;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::string entry = toLower(stem(list[i]));
            if (entry.empty()) {
                continue;
            }
            if (entry == needle) {
                exact.push_back(static_cast<int>(i));
            } else if (!needle.empty() && entry.find(needle) != std::string::npos) {
                partial.push_back(static_cast<int>(i));
            }
        }
        if (exact.size() == 1) {
            return { exact.front(), {}, {}, false };
        }
        if (!exact.empty()) {
            return { -1, exact, {}, false }; // scripts.lst genuinely lists the same basename twice
        }
        if (partial.size() == 1) {
            // Exactly one entry contains the fragment, so there is nothing to choose between —
            // resolve it, and flag that it came from a fragment so the caller can see what it got.
            return { partial.front(), {}, {}, true };
        }
        if (!partial.empty()) {
            return { -1, partial, {}, false };
        }
        return { -1, {}, "no script named '" + name + "' in scripts.lst", false };
    }
    if (programIndex < 0 || programIndex >= static_cast<int>(list.size())) {
        return { -1, {},
            "program index " + std::to_string(programIndex) + " out of range (have "
                + std::to_string(list.size())
                + " scripts). Note the SCRIPT_* constants in headers/scripts.h are 1-based: subtract 1, "
                  "or pass 'name' instead",
            false };
    }
    return { programIndex, {}, {}, false };
}

nlohmann::ordered_json ambiguousResult(resource::GameResources& resources, const Lst& scripts,
    const std::string& query, const std::vector<int>& candidates) {
    nlohmann::ordered_json root;
    root["query"] = query;
    root["ambiguous"] = true;
    auto matches = nlohmann::ordered_json::array();
    for (const int index : candidates) {
        matches.push_back(scriptIdentity(resources, scripts, index));
    }
    root["matches"] = std::move(matches);
    root["hint"] = "several scripts.lst entries match that name; re-run with an exact 'name' or a 'programIndex'";
    return root;
}

void emit(std::ostream& out, const nlohmann::ordered_json& root) {
    // Fallout 2 text is CP-1252, not UTF-8; `replace` substitutes U+FFFD for stray bytes so dump()
    // emits valid JSON instead of throwing on e.g. 0x85 ("…").
    out << root.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace) << "\n";
}

} // namespace geck::cli::script_lookup
