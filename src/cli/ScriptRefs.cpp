#include "cli/ScriptRefs.h"

#include "reader/TextParsing.h"
#include "resource/GameResources.h"
#include "resource/VfsSourceProvider.h"
#include "ssl/Corpus.h"
#include "ssl/VariableRefs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    constexpr std::size_t kMaxRefs = 400; // cap output so a common name can't flood the response

    bool sameName(std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    bool isScript(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".ssl";
    }

    // What one script contributes: refs as JSON, in source order.
    class ScriptSearch {
    public:
        ScriptSearch(const ssl::ScriptUnit& unit, const std::filesystem::path& path)
            : _unit(unit)
            , _graph(unit.program())
            , _path(path) {}

        std::vector<ordered_json> run(const ScriptRefsOptions& options) {
            if (options.kind == "gvar") {
                globalVariable(options.name);
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
                return call != nullptr && sameName(call->callee, options.name);
            }
            if (options.kind == "procedure") {
                return namesProcedure(expr, options.name);
            }
            // "macro": the outermost expression the macro produced, once per place the macro was used.
            const auto chain = _unit.macroChain(expr.range);
            return std::ranges::any_of(chain, [&options](const ssl::Macro* macro) { return sameName(macro->name, options.name); });
        }

        static bool namesProcedure(const ssl::Expr& expr, std::string_view name) {
            if (const auto* call = std::get_if<ssl::Call>(&expr.node)) {
                return sameName(call->callee, name);
            }
            if (const auto* bare = std::get_if<ssl::Name>(&expr.node)) {
                return sameName(bare->name, name);
            }
            if (const auto* ref = std::get_if<ssl::ProcedureRef>(&expr.node)) {
                return sameName(ref->name, name);
            }
            return false;
        }

        void globalVariable(const std::string& gvarName) {
            const auto target = targetIndex(gvarName);
            for (const ssl::VariableRef& ref : ssl::globalVariableRefs(_unit, _graph)) {
                const bool matches = target.has_value() && ref.index.has_value() ? *ref.index == *target : sameName(ref.name, gvarName);
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
            if (const ssl::Macro* macro = _unit.preprocessor().find(gvarName); macro != nullptr) {
                if (const auto value = _unit.evaluate(*macro); value.has_value()) {
                    return static_cast<int>(*value);
                }
            }
            return std::nullopt;
        }

        void add(const ssl::Procedure& procedure, ssl::TokenRange range) {
            add(procedure.name, _unit.writtenAt(range));
        }

        // A ref at `where`, once per written place (a macro expands into several expressions); nullptr
        // when that place is already reported.
        ordered_json* add(const std::string& procedure, const ssl::SourceLocation& where) {
            if (!_seen.emplace(procedure, where.file, where.line).second) {
                return nullptr;
            }
            ordered_json json{ { "script", _path.filename().string() } };
            if (const auto& written = _unit.sources().path(where.file); _unit.scriptFile() && written != _unit.sources().path(*_unit.scriptFile())) {
                json["file"] = written.generic_string(); // a header or template the script includes
            }
            json["line"] = where.line;
            json["procedure"] = procedure;
            json["text"] = geck::text::trim(std::string(_unit.sources().line(where.file, where.line)));
            _found.push_back(std::move(json));
            return &_found.back();
        }

        const ssl::ScriptUnit& _unit;
        const ssl::CallGraph _graph;
        const std::filesystem::path& _path;
        std::vector<ordered_json> _found;
        std::set<std::tuple<std::string, ssl::FileId, uint32_t>> _seen;
    };

} // namespace

int findScriptRefs(resource::GameResources& resources, const ScriptRefsOptions& options, std::ostream& out) {
    static const std::set<std::string, std::less<>> KINDS{ "gvar", "call", "procedure", "macro" };
    if (!KINDS.contains(options.kind) || options.name.empty()) {
        out << "script-refs: kind must be gvar, call, procedure or macro, and name must not be empty\n";
        return 1;
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
    ssl::loadScripts(provider, scripts, [&perScript, &options, &scripts](std::size_t index, const ssl::ScriptUnit& unit) {
        if (unit.scriptFile().has_value()) {
            perScript[index] = ScriptSearch(unit, scripts[index]).run(options);
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
            setters += ref.value("kind", "") == "set" ? 1 : 0;
            referencing.insert(ref["script"].get<std::string>());
            refs.push_back(ref);
        }
    }

    ordered_json root;
    root[options.kind == "gvar" ? "gvar" : "name"] = options.name;
    if (options.kind != "gvar") {
        root["kind"] = options.kind;
    }
    root["refs"] = std::move(refs);
    root["stats"] = { { "refs", root["refs"].size() }, { "scripts", referencing.size() }, { "scriptsRead", scripts.size() },
        { "capped", capped } };
    if (options.kind == "gvar") {
        root["stats"]["setters"] = setters;
    }
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    // Not used anywhere is a valid answer; nothing to read means no source tree is mounted, an error
    // so the MCP flags isError.
    return scripts.empty() ? 1 : 0;
}

} // namespace geck::cli
