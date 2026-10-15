#include "cli/ScriptLint.h"

#include "cli/ScriptLookup.h"
#include "format/lst/Lst.h"
#include "format/msg/Msg.h"
#include "resource/GameResources.h"
#include "resource/VfsSourceProvider.h"
#include "ssl/Corpus.h"
#include "ssl/MessageRefs.h"
#include "ssl/ScriptFacts.h"
#include "ssl/VariableRefs.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;
    using namespace script_lookup;

    constexpr std::size_t kMaxFindings = 1000; // across every script, for `all`

    struct Where {
        std::string file;
        uint32_t line = 0;
    };

    struct Finding {
        std::string severity; // "error", "warning" or "note"
        std::string check;
        std::string message;
        Where where;
        std::string procedure;
    };

    struct ProcedureInfo {
        std::string name;
        bool live = false;
        bool entryPoint = false;
        std::size_t parameters = 0;
        Where where;
        bool inScript = false; // written in the script itself, not a header or template it includes
    };

    template <typename Ref>
    struct Use {
        Ref ref;
        Where where;
    };

    // Everything the lint and the analysis need from one parse, with locations as paths so the report
    // outlives the parsed script.
    struct ScriptReport {
        std::string script;
        std::vector<Finding> syntax;
        std::vector<ProcedureInfo> procedures;
        std::vector<Use<ssl::VariableRef>> globals;
        std::vector<Use<ssl::VariableRef>> locals;
        std::vector<Use<ssl::MessageRef>> messages;
        std::vector<ssl::ScriptFacts::Override> overrides;
        std::map<int, std::string> localVarNames;
    };

    Where whereOf(const ssl::ScriptUnit& unit, const ssl::SourceLocation& location) {
        return { location.file != 0 ? unit.sources().path(location.file).generic_string() : std::string(), location.line };
    }

    template <typename Ref>
    std::vector<Use<Ref>> uses(const ssl::ScriptUnit& unit, std::vector<Ref> refs) {
        std::vector<Use<Ref>> out;
        for (Ref& ref : refs) {
            const Where where = whereOf(unit, ref.location);
            out.push_back({ std::move(ref), where });
        }
        return out;
    }

    ScriptReport report(const ssl::ScriptUnit& unit) {
        ScriptReport r;
        const auto scriptFile = unit.scriptFile();
        if (scriptFile.has_value()) {
            r.script = unit.sources().path(*scriptFile).generic_string();
        }
        for (const ssl::Diagnostic& d : unit.diagnostics()) {
            const bool error = d.severity == ssl::Diagnostic::Severity::Error;
            r.syntax.push_back({ error ? "error" : "warning", "syntax", d.message, whereOf(unit, d.location), {} });
        }
        const ssl::CallGraph graph(unit.program());
        for (const ssl::Procedure& procedure : unit.program().procedures) {
            if (!procedure.body.has_value()) {
                continue;
            }
            const ssl::SourceLocation written = unit.writtenAt(procedure.range);
            r.procedures.push_back({ procedure.name, graph.isLive(procedure.name), ssl::isEngineProcedure(procedure.name),
                procedure.parameters.size(), whereOf(unit, written), scriptFile.has_value() && written.file == *scriptFile });
        }
        r.globals = uses(unit, ssl::globalVariableRefs(unit, graph));
        r.locals = uses(unit, ssl::localVariableRefs(unit, graph));
        r.messages = uses(unit, ssl::messageRefs(unit, graph));
        ssl::ScriptFacts facts = ssl::readFacts(unit);
        r.overrides = std::move(facts.overrides);
        r.localVarNames = std::move(facts.localVarNames);
        return r;
    }

    // Turns a report into findings, with what the checks need from the game data.
    class Linter {
    public:
        Linter(resource::GameResources& resources, const Lst& scripts, std::string locale)
            : _resources(resources)
            , _scripts(scripts)
            , _locale(std::move(locale)) {}

        std::vector<Finding> findings(const ScriptReport& report, int programIndex, bool includeNotes) {
            std::vector<Finding> out = report.syntax;
            localVariables(report, programIndex, out);
            messages(report, out);
            if (includeNotes) {
                for (const ProcedureInfo& procedure : report.procedures) {
                    if (procedure.inScript && !procedure.live) {
                        out.push_back({ "note", "unused-procedure",
                            std::format("{} never runs: nothing calls it, so sslc leaves it out of the compiled script", procedure.name),
                            procedure.where, procedure.name });
                    }
                }
            }
            return out;
        }

        int localVariableCount(int programIndex) const {
            const auto& counts = _scripts.localVars();
            return programIndex >= 0 && static_cast<std::size_t>(programIndex) < counts.size() ? counts[static_cast<std::size_t>(programIndex)] : 0;
        }

    private:
        void localVariables(const ScriptReport& report, int programIndex, std::vector<Finding>& out) const {
            const int count = localVariableCount(programIndex);
            const std::string script = stem(_scripts.list().at(static_cast<std::size_t>(programIndex)));
            std::set<int> reported;
            for (const auto& use : report.locals) {
                if (!use.ref.index.has_value() || *use.ref.index < count || !reported.insert(*use.ref.index).second) {
                    continue;
                }
                const int index = *use.ref.index;
                const std::string what = use.ref.name.empty() ? std::format("local variable {}", index) : std::format("{} ({})", use.ref.name, index);
                const std::string message = count == 0
                    ? std::format("{} is used, but scripts.lst gives {} no local variables (no \"# local_vars=\"), so local_var and "
                                  "set_local_var do nothing",
                          what, script)
                    : std::format("{} is past the {} local variables scripts.lst gives {} (\"# local_vars={}\"); the engine does not "
                                  "check, so it reads and writes the next script's variables",
                          what, count, script, count);
                out.push_back({ "error", "local-vars", message, use.where, use.ref.procedure });
            }
        }

        void messages(const ScriptReport& report, std::vector<Finding>& out) {
            std::set<std::tuple<int, int, std::string, uint32_t>> reported;
            std::set<int> missingFiles;
            for (const auto& use : report.messages) {
                if (!use.ref.list.has_value() || !use.ref.number.has_value() || ssl::isBuiltInMessage(*use.ref.list, *use.ref.number)) {
                    continue;
                }
                const int list = *use.ref.list;
                const int number = *use.ref.number;
                if (!reported.emplace(list, number, use.where.file, use.where.line).second) {
                    continue;
                }
                if (list <= 0 || static_cast<std::size_t>(list) > _scripts.list().size()) {
                    out.push_back({ "error", "messages", std::format("{} reads message list {}, which names no scripts.lst line", use.ref.opcode, list),
                        use.where, use.ref.procedure });
                    continue;
                }
                const std::string name = stem(_scripts.list()[static_cast<std::size_t>(list - 1)]);
                const Msg* dialog = dialogOf(name);
                if (dialog == nullptr) {
                    if (missingFiles.insert(list).second) {
                        out.push_back({ "warning", "messages", std::format("dialog/{}.msg is not mounted, so its messages can't be checked", name),
                            use.where, use.ref.procedure });
                    }
                } else if (!dialog->getMessages().contains(number)) {
                    out.push_back({ "error", "messages",
                        std::format("message {} is not in dialog/{}.msg; the engine shows \"Error\" instead", number, name), use.where,
                        use.ref.procedure });
                }
            }
        }

        const Msg* dialogOf(const std::string& name) {
            if (const auto it = _dialogs.find(name); it != _dialogs.end()) {
                return it->second;
            }
            const Msg* msg = nullptr;
            const std::string path = "text/" + _locale + "/dialog/" + name + ".msg";
            try {
                msg = _resources.repository().load<Msg>(path);
            } catch (const std::exception& e) {
                spdlog::debug("lint: no dialog .msg at {}: {}", path, e.what());
            }
            _dialogs.try_emplace(name, msg);
            return msg;
        }

        resource::GameResources& _resources;
        const Lst& _scripts;
        std::string _locale;
        std::map<std::string, const Msg*, std::less<>> _dialogs;
    };

    ordered_json whereJson(const Where& where) {
        return { { "file", where.file }, { "line", where.line } };
    }

    ordered_json findingsJson(const std::vector<Finding>& findings) {
        auto out = ordered_json::array();
        for (const Finding& f : findings) {
            ordered_json json{ { "severity", f.severity }, { "check", f.check }, { "message", f.message } };
            json.update(whereJson(f.where));
            if (!f.procedure.empty()) {
                json["procedure"] = f.procedure;
            }
            out.push_back(std::move(json));
        }
        return out;
    }

    void count(const std::vector<Finding>& findings, ordered_json& summary) {
        for (const Finding& f : findings) {
            const std::string key = f.severity + "s";
            summary[key] = summary.value(key, 0) + 1;
        }
    }

    // The scripts.lst entries with a mounted source, as program index -> VFS path.
    std::map<int, std::string> sourcesByIndex(resource::GameResources& resources, const Lst& scripts) {
        const auto sources = indexSslSources(resources);
        std::map<int, std::string> out;
        for (std::size_t i = 0; i < scripts.list().size(); ++i) {
            if (const auto it = sources.find(toLower(stem(scripts.list()[i]))); it != sources.end()) {
                out.try_emplace(static_cast<int>(i), it->second);
            }
        }
        return out;
    }

    std::vector<ScriptReport> reports(resource::GameResources& resources, const std::vector<std::filesystem::path>& paths) {
        std::vector<ScriptReport> out(paths.size());
        const resource::VfsSourceProvider provider(resources.files());
        ssl::loadScripts(provider, paths, [&out](std::size_t index, const ssl::ScriptUnit& unit) { out[index] = report(unit); });
        return out;
    }

} // namespace

int lintScripts(resource::GameResources& resources, const LintOptions& options, std::ostream& out) {
    const Lst* scripts = loadScriptsLst(resources);
    if (scripts == nullptr) {
        out << "lint-script: scripts.lst not found (is the Fallout 2 data mounted?)\n";
        return 1;
    }
    const auto sources = sourcesByIndex(resources, *scripts);
    std::vector<int> targets;
    if (options.all) {
        for (const auto& [index, path] : sources) {
            targets.push_back(index);
        }
    } else {
        const Resolution resolved = resolveScript(*scripts, options.name, options.programIndex);
        if (!resolved.error.empty()) {
            out << "lint-script: " << resolved.error << "\n";
            return 1;
        }
        if (resolved.programIndex < 0) {
            emit(out, ambiguousResult(resources, *scripts, options.name, resolved.candidates));
            return 0;
        }
        targets.push_back(resolved.programIndex);
    }
    std::vector<std::filesystem::path> paths;
    for (const int index : targets) {
        const auto it = sources.find(index);
        if (it == sources.end()) {
            out << "lint-script: no .ssl source mounted for " << scripts->list()[static_cast<std::size_t>(index)]
                << " (mount a script-source tree such as the FRP scripts_src as a --data path)\n";
            return 1;
        }
        paths.emplace_back(it->second);
    }

    const std::vector<ScriptReport> parsed = reports(resources, paths);
    Linter linter(resources, *scripts, options.locale);
    ordered_json summary = ordered_json::object();
    if (!options.all) {
        const std::vector<Finding> findings = linter.findings(parsed.front(), targets.front(), options.includeNotes);
        ordered_json root = scriptIdentity(resources, *scripts, targets.front());
        count(findings, summary);
        root["summary"] = summary;
        root["findings"] = findingsJson(findings);
        emit(out, root);
        return 0;
    }

    auto results = ordered_json::array();
    std::size_t total = 0;
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        const std::vector<Finding> findings = linter.findings(parsed[i], targets[i], options.includeNotes);
        count(findings, summary);
        if (findings.empty() || total >= kMaxFindings) {
            continue;
        }
        total += findings.size();
        results.push_back({ { "name", stem(scripts->list()[static_cast<std::size_t>(targets[i])]) }, { "programIndex", targets[i] },
            { "findings", findingsJson(findings) } });
    }
    ordered_json root;
    root["scripts"] = parsed.size();
    root["summary"] = summary;
    root["capped"] = total >= kMaxFindings;
    root["results"] = std::move(results);
    emit(out, root);
    return 0;
}

std::optional<nlohmann::ordered_json> analyzeScript(resource::GameResources& resources, const Lst& scripts, int programIndex,
    const std::string& locale) {
    const auto sources = sourcesByIndex(resources, scripts);
    const auto source = sources.find(programIndex);
    if (source == sources.end()) {
        return std::nullopt;
    }
    const ScriptReport r = reports(resources, { std::filesystem::path(source->second) }).front();
    Linter linter(resources, scripts, locale);

    auto procedures = ordered_json::array();
    int includedLive = 0;
    for (const ProcedureInfo& p : r.procedures) {
        if (!p.inScript) {
            includedLive += p.live ? 1 : 0;
            continue;
        }
        procedures.push_back({ { "name", p.name }, { "line", p.where.line }, { "parameters", p.parameters }, { "live", p.live },
            { "entryPoint", p.entryPoint } });
    }
    const auto variableJson = [&r](const Use<ssl::VariableRef>& use) {
        ordered_json json{ { "name", use.ref.name }, { "kind", use.ref.kind == ssl::VariableRef::Kind::Set ? "set" : "get" } };
        json["index"] = use.ref.index.has_value() ? ordered_json(*use.ref.index) : ordered_json(nullptr);
        json["procedure"] = use.ref.procedure;
        if (use.where.file != r.script) {
            json["file"] = use.where.file;
        }
        json["line"] = use.where.line;
        if (!use.ref.macros.empty()) {
            json["via"] = use.ref.macros.front();
        }
        return json;
    };
    auto globals = ordered_json::array();
    for (const auto& use : r.globals) {
        globals.push_back(variableJson(use));
    }
    auto locals = ordered_json::array();
    for (const auto& use : r.locals) {
        locals.push_back(variableJson(use));
    }
    auto names = ordered_json::object();
    for (const auto& [index, name] : r.localVarNames) {
        names[std::to_string(index)] = name;
    }
    auto overrides = ordered_json::array();
    for (const auto& o : r.overrides) {
        ordered_json json{ { "field", o.field == ssl::ScriptFacts::Override::Field::AiPacket ? "aiPacket" : "team" } };
        json["value"] = o.value.has_value() ? ordered_json(*o.value) : ordered_json(nullptr);
        json["valueName"] = o.valueName;
        json["procedure"] = o.procedure;
        json["line"] = o.line;
        overrides.push_back(std::move(json));
    }

    ordered_json analysis;
    analysis["procedures"] = std::move(procedures);
    analysis["includedProceduresLive"] = includedLive;
    analysis["globalVariables"] = std::move(globals);
    analysis["localVariables"] = { { "count", linter.localVariableCount(programIndex) }, { "names", std::move(names) },
        { "uses", std::move(locals) } };
    analysis["selfOverrides"] = std::move(overrides);
    analysis["messageUses"] = r.messages.size();
    analysis["findings"] = findingsJson(linter.findings(r, programIndex, true));
    return analysis;
}

} // namespace geck::cli
