#include "cli/ScriptIntrospect.h"

#include "format/lst/Lst.h"
#include "format/msg/Msg.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace geck::cli {

namespace {
    using nlohmann::ordered_json;

    std::string toLower(std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // Basename of a path without directory or extension ("scripts_src/newreno/ncprosti.ssl" -> "ncprosti").
    std::string stem(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
        const auto dot = file.find_last_of('.');
        return dot == std::string::npos ? file : file.substr(0, dot);
    }

    // basename(lowercased) -> VFS path for every *.ssl in the mounted data (i.e. a mounted source tree).
    std::unordered_map<std::string, std::string> indexSslSources(resource::GameResources& resources) {
        std::unordered_map<std::string, std::string> index;
        for (const auto& path : resources.files().list("*")) {
            std::string ext = path.extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".ssl") {
                index.emplace(toLower(stem(path.generic_string())), path.generic_string());
            }
        }
        return index;
    }

    // Attach the .ssl source (from a mounted source tree) to `root`, or flag its absence.
    void attachSource(resource::GameResources& resources, const std::string& basename, ordered_json& root) {
        const auto index = indexSslSources(resources);
        const auto it = index.find(toLower(basename));
        if (it != index.end()) {
            if (const auto bytes = resources.files().readRawBytes(it->second); bytes.has_value()) {
                root["hasSource"] = true;
                root["sourcePath"] = it->second;
                root["source"] = std::string(bytes->begin(), bytes->end());
                return;
            }
        }
        root["hasSource"] = false;
        root["sourceHint"] = "mount a script-source patch (e.g. FRP scripts_src) as a --data path to read the .ssl";
    }

    // The dialog .msg lines for `basename` as [{id,text}], or an empty array if none.
    ordered_json loadDialog(resource::GameResources& resources, const std::string& basename, const std::string& locale) {
        auto dialog = ordered_json::array();
        const std::string path = "text/" + locale + "/dialog/" + basename + ".msg";
        try {
            if (const Msg* msg = resources.repository().load<Msg>(path); msg != nullptr) {
                for (const auto& [id, message] : msg->getMessages()) {
                    dialog.push_back({ { "id", id }, { "text", message.text } });
                }
            }
        } catch (const std::exception& e) {
            spdlog::debug("describe-script: no dialog .msg at {}: {}", path, e.what());
        }
        return dialog;
    }
} // namespace

int describeScript(resource::GameResources& resources, const DescribeScriptOptions& options, std::ostream& out) {
    const Lst* scripts = nullptr;
    try {
        scripts = resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
    } catch (const std::exception& e) {
        spdlog::debug("describe-script: scripts.lst load failed: {}", e.what());
    }
    if (scripts == nullptr) {
        out << "describe-script: scripts.lst not found (is the Fallout 2 data mounted?)\n";
        return 1;
    }
    const auto& list = scripts->list();
    if (options.programIndex < 1 || options.programIndex > static_cast<int>(list.size())) {
        out << "describe-script: program index " << options.programIndex << " out of range (1.." << list.size() << ")\n";
        return 1;
    }

    // script_id is 1-based; scripts.lst is stored 0-based (mirrors MapInfoPanel's resolution). The Lst
    // reader already strips the trailing comment, so the entry is just the script filename.
    const std::string& filename = list[static_cast<std::size_t>(options.programIndex) - 1];
    const std::string basename = stem(filename);

    ordered_json root;
    root["programIndex"] = options.programIndex;
    root["filename"] = filename;
    root["name"] = basename;
    attachSource(resources, basename, root);
    root["dialog"] = loadDialog(resources, basename, options.locale);
    // Fallout 2 text is CP-1252, not UTF-8; `replace` substitutes U+FFFD for stray bytes so dump()
    // emits valid JSON instead of throwing on e.g. 0x85 ("…").
    out << root.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
