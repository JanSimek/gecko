#include "cli/ScriptRefs.h"

#include "cli/ProtoName.h"
#include "reader/TextParsing.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"
#include "resource/VfsSourceProvider.h"
#include "ssl/Corpus.h"
#include "ssl/ObjectRefs.h"
#include "ssl/VariableRefs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    constexpr std::size_t kMaxRefs = 400; // cap output so a common name can't flood the response

    using text::equalsIgnoreCase;

    // A map name as the engine matches it (worldmap.cc wmMapMatchNameToIdx): lowercase, up to the first
    // '.', here also without a directory.
    std::string mapStem(const std::string& name) {
        const std::string file = text::toLower(std::filesystem::path(name).filename().string());
        return file.substr(0, file.find('.'));
    }

    // What a "map" search looks for, and the maps.txt index -> file name table, both read up front so
    // the per-script searches (on worker threads) only read them.
    struct MapTarget {
        bool any = false;         ///< name "*": every load_map
        std::optional<int> index; ///< the maps.txt index the target has
        std::string stem;         ///< its file name as the engine matches it
        std::map<int, std::string> fileNames;
    };

    bool isScript(const std::filesystem::path& path) {
        return text::toLower(path.extension().string()) == ".ssl";
    }

    // What one script contributes: refs as JSON, in source order.
    class ScriptSearch {
    public:
        ScriptSearch(const ssl::ScriptUnit& unit, const std::filesystem::path& path, const MapTarget& maps)
            : _unit(unit)
            , _graph(unit.program())
            , _path(path)
            , _maps(maps) {}

        std::vector<ordered_json> run(const ScriptRefsOptions& options) {
            if (options.kind == "gvar") {
                globalVariable(options.name);
            } else if (options.kind == "pid") {
                pids(options.name);
            } else if (options.kind == "map") {
                loadMaps();
            } else {
                forEachLiveExpression([this, &options](const ssl::Procedure& procedure, const ssl::Expr& expr) {
                    if (matches(options, expr)) {
                        add(procedure, expr.range);
                    }
                });
            }
            return std::move(_found);
        }

    private:
        void forEachLiveExpression(const std::function<void(const ssl::Procedure&, const ssl::Expr&)>& visit) const {
            for (const ssl::Procedure& procedure : _unit.program().procedures) {
                if (procedure.body.has_value() && _graph.isLive(procedure.name)) {
                    _graph.forEachLiveExpression(procedure, [&procedure, &visit](const ssl::Expr& expr) { visit(procedure, expr); });
                }
            }
        }

        bool matches(const ScriptRefsOptions& options, const ssl::Expr& expr) const {
            if (options.kind == "call") {
                const auto* call = std::get_if<ssl::Call>(&expr.node);
                return call != nullptr && equalsIgnoreCase(call->callee, options.name);
            }
            if (options.kind == "procedure") {
                return namesProcedure(expr, options.name);
            }
            // "macro": the outermost expression the macro produced, once per place the macro was used.
            const auto chain = _unit.macroChain(expr.range);
            return std::ranges::any_of(chain, [&options](const ssl::Macro* macro) { return equalsIgnoreCase(macro->name, options.name); });
        }

        static bool namesProcedure(const ssl::Expr& expr, std::string_view name) {
            if (const auto* call = std::get_if<ssl::Call>(&expr.node)) {
                return equalsIgnoreCase(call->callee, name);
            }
            if (const auto* bare = std::get_if<ssl::Name>(&expr.node)) {
                return equalsIgnoreCase(bare->name, name);
            }
            if (const auto* ref = std::get_if<ssl::ProcedureRef>(&expr.node)) {
                return equalsIgnoreCase(ref->name, name);
            }
            return false;
        }

        void globalVariable(const std::string& gvarName) {
            const auto target = targetIndex(gvarName);
            for (const ssl::VariableRef& ref : ssl::globalVariableRefs(_unit, _graph)) {
                const bool matches = target.has_value() && ref.index.has_value() ? *ref.index == *target : equalsIgnoreCase(ref.name, gvarName);
                if (!matches) {
                    continue;
                }
                ordered_json* json = add(ref.procedure, ref.location);
                if (json == nullptr) {
                    continue;
                }
                (*json)["kind"] = ref.kind == ssl::VariableRef::Kind::Set ? "set" : "get";
                if (!ref.macros.empty()) {
                    (*json)["via"] = ref.macros.front(); // the alias the script wrote, e.g. set_caravan_brahmin
                }
            }
        }

        // The number `gvarName` stands for in this script, when its headers define it.
        std::optional<int> targetIndex(const std::string& gvarName) const {
            if (const auto value = macroValue(gvarName); value.has_value()) {
                return static_cast<int>(*value);
            }
            return std::nullopt;
        }

        std::optional<int64_t> macroValue(const std::string& name) const {
            if (const ssl::Macro* macro = _unit.preprocessor().find(name); macro != nullptr) {
                return _unit.evaluate(*macro);
            }
            return std::nullopt;
        }

        // Constant PIDs: `pid` is a number, a PID_* macro this script's headers define, or "*" for all.
        void pids(const std::string& pid) {
            const bool any = pid == "*";
            std::optional<int64_t> target;
            if (!any) {
                target = text::parseInteger(pid);
                if (!target.has_value()) {
                    target = macroValue(pid);
                }
            }
            if (!any && !target.has_value()) {
                return; // a macro this script doesn't define names no PID here
            }
            for (const ssl::PidRef& ref : ssl::pidRefs(_unit, _graph)) {
                if (any || ref.pid == *target) {
                    if (ordered_json* json = add(ref.procedure, ref.location); json != nullptr) {
                        (*json)["via"] = ref.via;
                        (*json)["pid"] = std::format("0x{:08X}", static_cast<uint32_t>(ref.pid));
                    }
                }
            }
        }

    public:
        // Every load_map call, with its map resolved through maps.txt (by index or by file name).
        std::vector<ScriptMapTransition> transitions() {
            std::vector<ScriptMapTransition> out;
            for (const ssl::MapRef& ref : ssl::mapRefs(_unit, _graph)) {
                auto transition = place(ref.procedure, ref.location);
                if (!transition.has_value()) {
                    continue;
                }
                if (ref.index.has_value()) {
                    transition->mapIndex = static_cast<int>(*ref.index);
                }
                std::string stem = ref.name.has_value() ? mapStem(*ref.name) : std::string();
                if (const auto it = transition->mapIndex.has_value() ? _maps.fileNames.find(*transition->mapIndex) : _maps.fileNames.end();
                    stem.empty() && it != _maps.fileNames.end()) {
                    stem = mapStem(it->second);
                }
                transition->mapFile = stem.empty() ? std::string() : stem + ".map";
                transition->entrance = ref.entrance;
                out.push_back(std::move(*transition));
            }
            return out;
        }

    private:
        // load_map calls sending the player to the target map (by index or by file name).
        void loadMaps() {
            for (const ScriptMapTransition& t : transitions()) {
                const std::string stem = t.mapFile.empty() ? std::string() : mapStem(t.mapFile);
                if (const bool matches = _maps.any || (_maps.index.has_value() && t.mapIndex == _maps.index) || (!stem.empty() && stem == _maps.stem);
                    !matches) {
                    continue;
                }
                ordered_json json = placeJson(t);
                json["map"] = t.mapFile.empty() ? ordered_json(nullptr) : ordered_json(t.mapFile);
                if (t.mapIndex.has_value()) {
                    json["mapIndex"] = *t.mapIndex;
                }
                json["entrance"] = t.entrance.has_value() ? ordered_json(*t.entrance) : ordered_json(nullptr);
                _found.push_back(std::move(json));
            }
        }

        static ordered_json placeJson(const ScriptMapTransition& written) {
            ordered_json json{ { "script", written.script } };
            if (!written.file.empty()) {
                json["file"] = written.file;
            }
            json["line"] = written.line;
            json["procedure"] = written.procedure;
            json["text"] = written.text;
            return json;
        }

        void add(const ssl::Procedure& procedure, ssl::TokenRange range) {
            add(procedure.name, _unit.writtenAt(range));
        }

        // A ref at `where`, once per written place (a macro expands into several expressions); nullptr
        // when that place is already reported.
        ordered_json* add(const std::string& procedure, const ssl::SourceLocation& where) {
            auto written = place(procedure, where);
            if (!written.has_value()) {
                return nullptr;
            }
            _found.push_back(placeJson(*written));
            return &_found.back();
        }

        // Where a ref was written, once per written place (a macro expands into several expressions);
        // nullopt when that place is already reported.
        std::optional<ScriptMapTransition> place(const std::string& procedure, const ssl::SourceLocation& where) {
            if (!_seen.emplace(procedure, where.file, where.line).second) {
                return std::nullopt;
            }
            ScriptMapTransition written;
            written.script = _path.filename().string();
            if (const auto& path = _unit.sources().path(where.file); _unit.scriptFile().has_value() && path != _unit.sources().path(*_unit.scriptFile())) {
                written.file = path.generic_string(); // a header or template the script includes
            }
            written.line = where.line;
            written.procedure = procedure;
            written.text = geck::text::trim(std::string(_unit.sources().line(where.file, where.line)));
            return written;
        }

        const ssl::ScriptUnit& _unit;
        const ssl::CallGraph _graph;
        const std::filesystem::path& _path;
        const MapTarget& _maps;
        std::vector<ordered_json> _found;
        std::set<std::tuple<std::string, ssl::FileId, uint32_t>> _seen;
    };

    // The map a "map" search names (file name or maps.txt index, "*" for all), with the index -> file
    // name table.
    MapTarget mapTarget(resource::GameResources& resources, const std::string& name) {
        MapTarget maps;
        const resource::MapNameResolver resolver(resources);
        for (const resource::MapName& map : resolver.allMaps()) {
            maps.fileNames.try_emplace(map.index, map.fileName);
        }
        maps.any = name == "*";
        if (const auto number = text::parseInteger(name); number.has_value()) {
            maps.index = static_cast<int>(*number);
            maps.stem = mapStem(resolver.fileNameOf(*maps.index));
        } else if (!maps.any) {
            maps.stem = mapStem(name);
            if (const int index = resolver.indexOf(maps.stem + ".map"); index >= 0) {
                maps.index = index;
            }
        }
        return maps;
    }

    struct Collected {
        ordered_json refs = ordered_json::array();
        std::size_t scripts = 0; ///< scripts with a ref
        int setters = 0;         ///< gvar writes
        bool capped = false;
    };

    // The per-script refs in script order, up to kMaxRefs; a PID ref gets its proto's name, read here
    // rather than on the worker threads (the repository cache).
    Collected collect(resource::GameResources& resources, const std::vector<std::vector<ordered_json>>& perScript, bool namePids) {
        Collected out;
        std::map<uint32_t, std::string> protoNames;
        std::set<std::string> referencing;
        for (const auto& ref : perScript | std::views::join) {
            if (out.refs.size() >= kMaxRefs) {
                out.capped = true;
                break;
            }
            out.setters += ref.value("kind", "") == "set" ? 1 : 0;
            referencing.insert(ref["script"].get<std::string>());
            out.refs.push_back(ref);
            if (namePids) {
                const auto pid = static_cast<uint32_t>(std::stoul(ref["pid"].get<std::string>(), nullptr, 16));
                auto [name, fresh] = protoNames.try_emplace(pid);
                if (fresh) {
                    name->second = protoDisplayName(resources, pid);
                }
                out.refs.back()["name"] = name->second;
            }
        }
        out.scripts = referencing.size();
        return out;
    }

    // Every mounted .ssl, sorted.
    std::vector<std::filesystem::path> mountedScripts(resource::GameResources& resources) {
        std::vector<std::filesystem::path> scripts;
        try {
            for (const auto& path : resources.files().list("*")) {
                if (isScript(path)) {
                    scripts.push_back(path);
                }
            }
        } catch (const std::exception&) {
            // no data mounted -> treated as no sources
        }
        std::ranges::sort(scripts);
        return scripts;
    }

} // namespace

int findScriptRefs(resource::GameResources& resources, const ScriptRefsOptions& options, std::ostream& out) {
    static const std::set<std::string, std::less<>> KINDS{ "gvar", "call", "procedure", "macro", "pid", "map" };
    if (!KINDS.contains(options.kind) || options.name.empty()) {
        out << "script-refs: kind must be gvar, call, procedure, macro, pid or map, and name must not be empty\n";
        return 1;
    }
    const MapTarget maps = options.kind == "map" ? mapTarget(resources, options.name) : MapTarget{};
    const std::vector<std::filesystem::path> scripts = mountedScripts(resources);

    std::vector<std::vector<ordered_json>> perScript(scripts.size());
    const resource::VfsSourceProvider provider(resources.files());
    ssl::loadScripts(provider, scripts, [&perScript, &options, &scripts, &maps](std::size_t index, const ssl::ScriptUnit& unit) {
        if (unit.scriptFile().has_value()) {
            perScript[index] = ScriptSearch(unit, scripts[index], maps).run(options);
        }
    });

    const Collected collected = collect(resources, perScript, options.kind == "pid");
    ordered_json root;
    root[options.kind == "gvar" ? "gvar" : "name"] = options.name;
    if (options.kind != "gvar") {
        root["kind"] = options.kind;
    }
    root["refs"] = collected.refs;
    root["stats"] = { { "refs", collected.refs.size() }, { "scripts", collected.scripts }, { "scriptsRead", scripts.size() },
        { "capped", collected.capped } };
    if (options.kind == "gvar") {
        root["stats"]["setters"] = collected.setters;
    }
    if (options.kind == "map" && !maps.any) {
        root["map"] = maps.stem.empty() ? ordered_json(nullptr) : ordered_json(maps.stem + ".map");
        root["mapIndex"] = maps.index.has_value() ? ordered_json(*maps.index) : ordered_json(nullptr);
    }
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    // Not used anywhere is a valid answer; nothing to read means no source tree is mounted, an error
    // so the MCP flags isError.
    return scripts.empty() ? 1 : 0;
}

std::set<std::string, std::less<>> scriptsCalling(resource::GameResources& resources, const std::string& name,
    std::size_t* scriptsRead) {
    const std::vector<std::filesystem::path> scripts = mountedScripts(resources);
    if (scriptsRead != nullptr) {
        *scriptsRead = scripts.size();
    }
    const ScriptRefsOptions options{ "call", name };
    std::vector<char> calls(scripts.size(), 0);
    const resource::VfsSourceProvider provider(resources.files());
    ssl::loadScripts(provider, scripts, [&calls, &options, &scripts](std::size_t index, const ssl::ScriptUnit& unit) {
        if (unit.scriptFile().has_value()) {
            calls[index] = ScriptSearch(unit, scripts[index], MapTarget{}).run(options).empty() ? 0 : 1;
        }
    });
    std::set<std::string, std::less<>> stems;
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        if (calls[i] != 0) {
            stems.insert(text::toLower(scripts[i].stem().string()));
        }
    }
    return stems;
}

std::vector<ScriptMapTransition> scriptMapTransitions(resource::GameResources& resources, std::size_t* scriptsRead) {
    const MapTarget maps = mapTarget(resources, "*");
    const std::vector<std::filesystem::path> scripts = mountedScripts(resources);
    if (scriptsRead != nullptr) {
        *scriptsRead = scripts.size();
    }
    std::vector<std::vector<ScriptMapTransition>> perScript(scripts.size());
    const resource::VfsSourceProvider provider(resources.files());
    ssl::loadScripts(provider, scripts, [&perScript, &scripts, &maps](std::size_t index, const ssl::ScriptUnit& unit) {
        if (unit.scriptFile().has_value()) {
            perScript[index] = ScriptSearch(unit, scripts[index], maps).transitions();
        }
    });
    std::vector<ScriptMapTransition> out;
    for (auto& found : perScript) {
        std::ranges::move(found, std::back_inserter(out));
    }
    return out;
}

} // namespace geck::cli
