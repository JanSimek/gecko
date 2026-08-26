#include "cli/ScriptIntrospect.h"

#include "cli/MapAnalyzer.h" // listMapPaths
#include "cli/MapLoad.h"     // loadMap
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "format/msg/Msg.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>
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
                index.try_emplace(toLower(stem(path.generic_string())), path.generic_string());
            }
        }
        return index;
    }

    // Attach the .ssl source (from a mounted source tree) to `root`, or flag its absence.
    void attachSource(resource::GameResources& resources, const std::string& basename, ordered_json& root) {
        const auto index = indexSslSources(resources);
        if (const auto it = index.find(toLower(basename)); it != index.end()) {
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

    // --- script resolution -------------------------------------------------------

    const Lst* loadScriptsLst(resource::GameResources& resources) {
        try {
            return resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
        } catch (const std::exception& e) {
            spdlog::debug("scripts.lst load failed: {}", e.what());
            return nullptr;
        }
    }

    // The identity block every script-shaped result leads with. `sslConstant` is the 1-based number
    // the FRP headers/scripts.h SCRIPT_* constants carry, echoed next to the 0-based programIndex so
    // the two bases can be told apart on sight instead of by silently naming the wrong script.
    ordered_json scriptIdentity(resource::GameResources& resources, const Lst& scripts, int programIndex) {
        const std::string& filename = scripts.list()[static_cast<std::size_t>(programIndex)];
        ordered_json identity;
        identity["programIndex"] = programIndex;
        identity["sslConstant"] = programIndex + 1;
        identity["filename"] = filename;
        identity["name"] = stem(filename);
        const std::string description = resource::scriptDescription(resources, programIndex);
        identity["description"] = description.empty() ? ordered_json(nullptr) : ordered_json(description);
        return identity;
    }

    // The outcome of turning a caller's `name`/`programIndex` into one scripts.lst entry: exactly one
    // index, or a candidate list when a name fragment was ambiguous, or a hard error.
    struct Resolution {
        int programIndex = -1;       ///< >= 0 when a single script was selected
        std::vector<int> candidates; ///< non-empty when `name` matched several entries
        std::string error;           ///< non-empty when nothing matched at all
        bool fromFragment = false;   ///< resolved from a substring, not an exact basename
    };

    // Resolve by name (exact basename first, then substring — so a half-remembered name still helps)
    // or, failing that, by 0-based program index.
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

    // A candidate-list result: the caller gave a fragment that names several scripts, so report them
    // rather than picking one.
    ordered_json ambiguousResult(resource::GameResources& resources, const Lst& scripts,
        const std::string& query, const std::vector<int>& candidates) {
        ordered_json root;
        root["query"] = query;
        root["ambiguous"] = true;
        auto matches = ordered_json::array();
        for (const int index : candidates) {
            matches.push_back(scriptIdentity(resources, scripts, index));
        }
        root["matches"] = std::move(matches);
        root["hint"] = "several scripts.lst entries match that name; re-run with an exact 'name' or a 'programIndex'";
        return root;
    }

    void emit(std::ostream& out, const ordered_json& root) {
        // Fallout 2 text is CP-1252, not UTF-8; `replace` substitutes U+FFFD for stray bytes so dump()
        // emits valid JSON instead of throwing on e.g. 0x85 ("…").
        out << root.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    }

    // --- find_script -------------------------------------------------------------

    // Every placement of `programIndex` in one map: the map's own header script, plus per-section
    // map_scripts counts. Returns nullopt when the map does not reference the script at all.
    std::optional<ordered_json> placementsIn(const Map& map, int programIndex) {
        const auto& mapFile = map.getMapFile();
        // The header's script_id is 1-based (the engine runs scripts.lst[script_id - 1], fallout2-ce
        // map.cc), unlike MapScript::script_id below, which is already the 0-based program index.
        const bool asMapScript = mapFile.header.script_id > 0
            && mapFile.header.script_id - 1 == programIndex;

        auto sections = ordered_json::array();
        int total = 0;
        for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
            int count = 0;
            for (const MapScript& script : mapFile.map_scripts[section]) {
                if (static_cast<int>(script.script_id) == programIndex) {
                    ++count;
                }
            }
            if (count > 0) {
                // The map_scripts section index is the script type (MapScript.h).
                const auto type = static_cast<MapScript::ScriptType>(section);
                sections.push_back({ { "section", std::string(MapScript::toString(type)) }, { "count", count } });
                total += count;
            }
        }
        if (!asMapScript && total == 0) {
            return std::nullopt;
        }
        ordered_json entry;
        entry["asMapScript"] = asMapScript;
        entry["sections"] = std::move(sections);
        entry["instances"] = total;
        return entry;
    }

    // maps.txt/map.msg lookup, or nullopt when maps.txt isn't mounted (friendly names are a bonus,
    // never a reason to fail the scan).
    std::optional<resource::MapNameResolver> makeNameResolver(resource::GameResources& resources) {
        try {
            return resource::MapNameResolver(resources);
        } catch (const std::exception& e) {
            spdlog::debug("find-script: no map name resolver: {}", e.what());
            return std::nullopt;
        }
    }

    // --- find_text ---------------------------------------------------------------

    // Substring (default) or ECMAScript-regex matching, case-insensitive unless asked otherwise.
    class TextMatcher {
    public:
        static std::optional<TextMatcher> make(const FindTextOptions& options, std::string& error) {
            TextMatcher matcher;
            matcher._useRegex = options.regex;
            matcher._caseSensitive = options.caseSensitive;
            if (options.regex) {
                auto flags = std::regex::ECMAScript;
                if (!options.caseSensitive) {
                    flags |= std::regex::icase;
                }
                try {
                    matcher._regex = std::regex(options.pattern, flags);
                } catch (const std::regex_error& e) {
                    error = std::string("invalid regex: ") + e.what();
                    return std::nullopt;
                }
            } else {
                matcher._needle = options.caseSensitive ? options.pattern : toLower(options.pattern);
            }
            return matcher;
        }

        [[nodiscard]] bool matches(const std::string& haystack) const {
            if (_useRegex) {
                return std::regex_search(haystack, _regex);
            }
            if (_caseSensitive) {
                return haystack.find(_needle) != std::string::npos;
            }
            return toLower(haystack).find(_needle) != std::string::npos;
        }

    private:
        bool _useRegex = false;
        bool _caseSensitive = false;
        std::string _needle;
        std::regex _regex;
    };

    // Which corpora a scope name selects.
    struct TextScope {
        bool dialog = false;
        bool game = false;
        bool source = false;
    };

    TextScope parseScope(const std::string& scope) {
        if (scope == "dialog") {
            return { true, false, false };
        }
        if (scope == "game") {
            return { false, true, false };
        }
        if (scope == "source") {
            return { false, false, true };
        }
        return { true, true, true }; // "all", and anything unrecognised
    }

    // VFS paths come back with a leading slash ("/text/english/dialog/x.msg") whose presence depends
    // on the mount, so compare against the path with it stripped.
    bool hasPrefix(const std::string& path, const std::string& prefix) {
        const std::string_view rest = std::string_view(path).substr(path.starts_with('/') ? 1 : 0);
        return rest.size() >= prefix.size() && toLower(std::string(rest.substr(0, prefix.size()))) == prefix;
    }

    std::string extensionOf(const std::filesystem::path& path) {
        return toLower(path.extension().string());
    }

    std::string trimmed(const std::string& line) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const auto last = line.find_last_not_of(" \t\r\n");
        return line.substr(first, last - first + 1);
    }

    // Accumulates hits up to the caller's limit, so a broad pattern truncates instead of flooding.
    struct MatchSink {
        ordered_json matches = ordered_json::array();
        int limit = 0;
        int total = 0;
        int filesSearched = 0;

        [[nodiscard]] bool full() const { return total >= limit; }
        void add(ordered_json match) {
            ++total;
            if (static_cast<int>(matches.size()) < limit) {
                matches.push_back(std::move(match));
            }
        }
    };

    // `kind` is "dialog" (a per-script .msg, so the basename IS the script) or "game" (item/perk/quest
    // text owned by no script). Only the former gets a `script`, so a caller cannot feed "perk" into
    // describe_script and get an unrelated hit; `file` is always the basename.
    void searchMsg(resource::GameResources& resources, const std::string& path, const char* kind,
        const TextMatcher& matcher, MatchSink& sink) {
        const Msg* msg = nullptr;
        try {
            msg = resources.repository().load<Msg>(path);
        } catch (const std::exception& e) {
            spdlog::debug("find-text: cannot read {}: {}", path, e.what());
            return;
        }
        if (msg == nullptr) {
            return;
        }
        ++sink.filesSearched;
        for (const auto& [id, message] : msg->getMessages()) {
            if (sink.full()) {
                return;
            }
            if (matcher.matches(message.text)) {
                const bool scriptOwned = std::string_view(kind) == "dialog";
                sink.add({ { "kind", kind },
                    { "script", scriptOwned ? ordered_json(stem(path)) : ordered_json(nullptr) },
                    { "file", stem(path) }, { "path", path }, { "id", id }, { "text", message.text } });
            }
        }
    }

    void searchSource(resource::GameResources& resources, const std::string& path,
        const TextMatcher& matcher, MatchSink& sink) {
        const auto bytes = resources.files().readRawBytes(path);
        if (!bytes.has_value()) {
            return;
        }
        ++sink.filesSearched;
        const std::string text(bytes->begin(), bytes->end());
        std::size_t lineStart = 0;
        int lineNumber = 1;
        while (lineStart <= text.size() && !sink.full()) {
            const std::size_t lineEnd = text.find('\n', lineStart);
            const std::string line = text.substr(lineStart,
                (lineEnd == std::string::npos ? text.size() : lineEnd) - lineStart);
            if (matcher.matches(line)) {
                sink.add({ { "kind", "source" }, { "script", stem(path) }, { "file", stem(path) },
                    { "path", path }, { "line", lineNumber }, { "text", trimmed(line) } });
            }
            if (lineEnd == std::string::npos) {
                break;
            }
            lineStart = lineEnd + 1;
            ++lineNumber;
        }
    }
} // namespace

int describeScript(resource::GameResources& resources, const DescribeScriptOptions& options, std::ostream& out) {
    const Lst* scripts = loadScriptsLst(resources);
    if (scripts == nullptr) {
        out << "describe-script: scripts.lst not found (is the Fallout 2 data mounted?)\n";
        return 1;
    }
    const Resolution resolved = resolveScript(*scripts, options.name, options.programIndex);
    if (!resolved.error.empty()) {
        out << "describe-script: " << resolved.error << "\n";
        return 1;
    }
    if (resolved.programIndex < 0) {
        // The candidate list is a usable answer, not a failure: returning nonzero would make the MCP
        // wrapper flag isError and contradict the tool's own contract. findScript agrees.
        emit(out, ambiguousResult(resources, *scripts, options.name, resolved.candidates));
        return 0;
    }

    // programIndex is the 0-based scripts.lst index (== a critter/object's MapScript.script_id), which
    // the engine's scriptsGetFileName and the editor's SelectionPanel both index directly. The Lst
    // reader already strips the trailing comment, so the entry is just the script filename.
    ordered_json root = scriptIdentity(resources, *scripts, resolved.programIndex);
    if (resolved.fromFragment) {
        root["matchedFragment"] = options.name;
    }
    const std::string basename = root["name"].get<std::string>();
    attachSource(resources, basename, root);
    root["dialog"] = loadDialog(resources, basename, options.locale);
    emit(out, root);
    return 0;
}

int findScript(resource::GameResources& resources, const FindScriptOptions& options, std::ostream& out) {
    const Lst* scripts = loadScriptsLst(resources);
    if (scripts == nullptr) {
        out << "find-script: scripts.lst not found (is the Fallout 2 data mounted?)\n";
        return 1;
    }
    const Resolution resolved = resolveScript(*scripts, options.name, options.programIndex);
    if (!resolved.error.empty()) {
        out << "find-script: " << resolved.error << "\n";
        return 1;
    }
    if (resolved.programIndex < 0) {
        // A fragment that names several scripts is a useful answer in itself, not a failure: the
        // candidate list is how you go from a half-remembered name to an exact one.
        emit(out, ambiguousResult(resources, *scripts, options.name, resolved.candidates));
        return 0;
    }

    ordered_json root;
    root["script"] = scriptIdentity(resources, *scripts, resolved.programIndex);
    if (resolved.fromFragment) {
        root["matchedFragment"] = options.name;
    }
    if (options.resolveOnly) {
        root["placements"] = nullptr;
        root["note"] = "resolveOnly: the map scan was skipped";
        emit(out, root);
        return 0;
    }

    const std::vector<std::string> mapPaths = options.maps.empty()
        ? listMapPaths(resources.files())
        : options.maps;
    const auto names = makeNameResolver(resources);
    auto placements = ordered_json::array();
    auto unreadable = ordered_json::array();
    int mapsScanned = 0;
    int instances = 0;
    for (const auto& mapPath : mapPaths) {
        std::string loadError;
        const std::unique_ptr<Map> map = loadMap(resources, mapPath, &loadError);
        if (map == nullptr) {
            // A partial scan beats no answer, but a silently skipped map could be the one holding the
            // script, so absence of placements is only trustworthy alongside this list.
            unreadable.push_back({ { "map", mapPath }, { "reason", loadError } });
            continue;
        }
        ++mapsScanned;
        auto entry = placementsIn(*map, resolved.programIndex);
        if (!entry.has_value()) {
            continue;
        }
        std::string display;
        if (names.has_value()) {
            const int index = names->indexOf(std::filesystem::path(mapPath).filename().string());
            if (index >= 0) {
                display = names->displayName(index, 0);
            }
        }
        ordered_json placement;
        placement["map"] = mapPath;
        placement["displayName"] = display.empty() ? ordered_json(nullptr) : ordered_json(display);
        placement.update(*entry);
        instances += (*entry)["instances"].get<int>();
        placements.push_back(std::move(placement));
    }
    root["placements"] = std::move(placements);
    root["mapCount"] = static_cast<int>(root["placements"].size());
    root["instanceCount"] = instances;
    root["mapsScanned"] = mapsScanned;
    root["mapsUnreadable"] = std::move(unreadable);
    emit(out, root);
    return 0;
}

int findText(resource::GameResources& resources, const FindTextOptions& options, std::ostream& out) {
    if (options.pattern.empty()) {
        out << "find-text: 'pattern' must not be empty\n";
        return 1;
    }
    std::string error;
    const auto matcher = TextMatcher::make(options, error);
    if (!matcher.has_value()) {
        out << "find-text: " << error << "\n";
        return 1;
    }

    const TextScope scope = parseScope(options.scope);
    const std::string dialogPrefix = "text/" + toLower(options.locale) + "/dialog/";
    const std::string gamePrefix = "text/" + toLower(options.locale) + "/game/";

    std::vector<std::string> msgDialog;
    std::vector<std::string> msgGame;
    std::vector<std::string> sslPaths;
    for (const auto& path : resources.files().list("*")) {
        const std::string generic = path.generic_string();
        const std::string ext = extensionOf(path);
        if (ext == ".msg") {
            if (scope.dialog && hasPrefix(generic, dialogPrefix)) {
                msgDialog.push_back(generic);
            } else if (scope.game && hasPrefix(generic, gamePrefix)) {
                msgGame.push_back(generic);
            }
        } else if (ext == ".ssl" && scope.source) {
            sslPaths.push_back(generic);
        }
    }
    std::ranges::sort(msgDialog);
    std::ranges::sort(msgGame);
    std::ranges::sort(sslPaths);

    MatchSink sink;
    sink.limit = options.limit > 0 ? options.limit : 200;
    for (const auto& path : msgDialog) {
        if (sink.full()) {
            break;
        }
        searchMsg(resources, path, "dialog", *matcher, sink);
    }
    for (const auto& path : msgGame) {
        if (sink.full()) {
            break;
        }
        searchMsg(resources, path, "game", *matcher, sink);
    }
    for (const auto& path : sslPaths) {
        if (sink.full()) {
            break;
        }
        searchSource(resources, path, *matcher, sink);
    }

    ordered_json root;
    root["pattern"] = options.pattern;
    root["scope"] = options.scope;
    root["regex"] = options.regex;
    root["caseSensitive"] = options.caseSensitive;
    root["filesSearched"] = sink.filesSearched;
    root["matchCount"] = static_cast<int>(sink.matches.size());
    root["truncated"] = sink.full();
    root["matches"] = std::move(sink.matches);
    emit(out, root);
    return 0;
}

} // namespace geck::cli
