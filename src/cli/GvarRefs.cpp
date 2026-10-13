#include "cli/GvarRefs.h"

#include "reader/TextParsing.h"
#include "resource/GameResources.h"
#include "resource/VfsSourceProvider.h"
#include "ssl/Corpus.h"
#include "ssl/VariableRefs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    constexpr std::size_t kMaxRefs = 400; // cap output so a common gvar can't flood the response

    bool sameName(std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    bool isScript(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".ssl";
    }

    // The number `gvarName` stands for in this script, when the script's headers define it.
    std::optional<int> targetIndex(const ssl::ScriptUnit& unit, const std::string& gvarName) {
        if (const ssl::Macro* macro = unit.preprocessor().find(gvarName); macro != nullptr) {
            if (const auto value = unit.evaluate(*macro); value.has_value()) {
                return static_cast<int>(*value);
            }
        }
        return std::nullopt;
    }

    // The accesses to `gvarName` in one script, as JSON refs.
    std::vector<ordered_json> scriptRefs(const ssl::ScriptUnit& unit, const std::string& gvarName, const std::filesystem::path& path) {
        std::vector<ordered_json> found;
        const auto target = targetIndex(unit, gvarName);
        const ssl::CallGraph graph(unit.program());
        for (const ssl::VariableRef& ref : ssl::globalVariableRefs(unit, graph)) {
            const bool matches = target.has_value() && ref.index.has_value() ? *ref.index == *target : sameName(ref.name, gvarName);
            if (!matches) {
                continue;
            }
            ordered_json json{ { "script", path.filename().string() } };
            const auto& written = unit.sources().path(ref.location.file);
            if (written != unit.sources().path(*unit.scriptFile())) {
                json["file"] = written.generic_string(); // a header or template the script includes
            }
            json["line"] = ref.location.line;
            json["procedure"] = ref.procedure;
            json["kind"] = ref.kind == ssl::VariableRef::Kind::Set ? "set" : "get";
            if (!ref.macros.empty()) {
                json["via"] = ref.macros.front(); // the macro alias the script wrote, e.g. set_caravan_brahmin
            }
            json["text"] = geck::text::trim(std::string(unit.sources().line(ref.location.file, ref.location.line)));
            found.push_back(std::move(json));
        }
        return found;
    }

} // namespace

int findGvarRefs(resource::GameResources& resources, const std::string& gvarName, std::ostream& out) {
    if (gvarName.empty()) {
        out << "{\"gvar\":\"\",\"refs\":[],\"stats\":{\"refs\":0,\"setters\":0,\"scripts\":0}}\n";
        return 1; // an empty name would match every access
    }

    std::vector<std::filesystem::path> scripts;
    try {
        for (const auto& path : resources.files().list("*")) {
            if (isScript(path)) {
                scripts.push_back(path);
            }
        }
    } catch (const std::exception&) {
        // no data mounted -> treated as no sources below
    }
    std::ranges::sort(scripts);

    std::vector<std::vector<ordered_json>> perScript(scripts.size());
    const resource::VfsSourceProvider provider(resources.files());
    ssl::loadScripts(provider, scripts, [&perScript, &gvarName, &scripts](std::size_t index, const ssl::ScriptUnit& unit) {
        if (unit.scriptFile().has_value()) {
            perScript[index] = scriptRefs(unit, gvarName, scripts[index]);
        }
    });

    auto refs = ordered_json::array();
    int setters = 0;
    std::set<std::string> referencing;
    bool capped = false;
    for (const auto& found : perScript) {
        for (const auto& ref : found) {
            if (refs.size() >= kMaxRefs) {
                capped = true;
                break;
            }
            setters += ref["kind"] == "set" ? 1 : 0;
            referencing.insert(ref["script"].get<std::string>());
            refs.push_back(ref);
        }
    }

    ordered_json root;
    root["gvar"] = gvarName;
    root["refs"] = std::move(refs);
    root["stats"] = { { "refs", root["refs"].size() }, { "setters", setters }, { "scripts", referencing.size() },
        { "scriptsRead", scripts.size() }, { "capped", capped } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    // An unused gvar in a mounted source tree is a valid answer; nothing to read means no source tree
    // is mounted, an error so the MCP flags isError.
    return scripts.empty() ? 1 : 0;
}

} // namespace geck::cli
