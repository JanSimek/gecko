#include "cli/GvarRefs.h"

#include "reader/TextParsing.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    constexpr std::size_t kMaxRefs = 400; // cap output so a common gvar can't flood the response

    std::string baseName(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    bool isWordChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    // Does `name` occur in `line` as a whole identifier (not a substring of a longer name)?
    bool referencesWord(const std::string& line, const std::string& name) {
        for (auto pos = line.find(name); pos != std::string::npos; pos = line.find(name, pos + 1)) {
            const bool leftOk = pos == 0 || !isWordChar(line[pos - 1]);
            const bool rightOk = pos + name.size() >= line.size() || !isWordChar(line[pos + name.size()]);
            if (leftOk && rightOk) {
                return true;
            }
        }
        return false;
    }

    // Scan one script's text for whole-word references to `gvarName`, appending one ref per matching
    // line. Returns false once the global cap is reached.
    bool scanScript(const std::string& text, const std::string& gvarName, const std::string& script,
        ordered_json& refs, int& setters, std::set<std::string>& scripts) {
        std::istringstream stream(text);
        std::string line;
        int lineNo = 0;
        while (std::getline(stream, line)) {
            ++lineNo;
            const std::string code = line.substr(0, line.find("//")); // ignore // line comments
            if (!referencesWord(code, gvarName)) {
                continue;
            }
            const bool isSet = code.find("set_global_var") != std::string::npos;
            if (isSet) {
                ++setters;
            }
            scripts.insert(script);
            refs.push_back({ { "script", script }, { "line", lineNo },
                { "kind", isSet ? "set" : "get" }, { "text", geck::text::trim(line) } });
            if (refs.size() >= kMaxRefs) {
                return false;
            }
        }
        return true;
    }

    // The source text of `path` if it is a scannable script (.ssl/.h) and readable, else nullopt.
    // Headers are included because many gvars are only touched via macro aliases defined there.
    std::optional<std::string> readScript(resource::GameResources& resources, const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".ssl" && ext != ".h") {
            return std::nullopt;
        }
        const auto bytes = resources.files().readRawBytes(path);
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        return std::string(bytes->begin(), bytes->end());
    }

} // namespace

int findGvarRefs(resource::GameResources& resources, const std::string& gvarName, std::ostream& out) {
    if (gvarName.empty()) {
        out << "{\"gvar\":\"\",\"refs\":[],\"stats\":{\"refs\":0,\"setters\":0,\"scripts\":0}}\n";
        return 1; // an empty name would match every line
    }

    auto refs = ordered_json::array();
    int setters = 0;
    std::set<std::string> scripts;
    bool capped = false;
    bool scannedAny = false; // did we read at least one source file?

    try {
        for (const auto& path : resources.files().list("*")) {
            const auto text = readScript(resources, path);
            if (!text.has_value()) {
                continue;
            }
            scannedAny = true;
            if (!scanScript(*text, gvarName, baseName(path.generic_string()), refs, setters, scripts)) {
                capped = true;
                break;
            }
        }
    } catch (const std::exception&) {
        // no data / source tree mounted -> empty result below
    }

    if (refs.empty()) {
        out << "{\"gvar\":" << ordered_json(gvarName).dump()
            << ",\"refs\":[],\"stats\":{\"refs\":0,\"setters\":0,\"scripts\":0}}\n";
        // scanned sources but found nothing = a valid "unused" answer (success); nothing scanned =
        // no source tree mounted (error, so the MCP flags isError).
        return scannedAny ? 0 : 1;
    }

    ordered_json root;
    root["gvar"] = gvarName;
    root["refs"] = std::move(refs);
    root["stats"] = { { "refs", root["refs"].size() }, { "setters", setters },
        { "scripts", scripts.size() }, { "capped", capped } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
