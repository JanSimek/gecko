#include "cli/ScriptLint.h"

#include "cli/ScriptLookup.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "resource/ScriptLint.h"

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
    using resource::ScriptFinding;
    using resource::ScriptReport;

    constexpr std::size_t kMaxFindings = 1000; // across every script, for `all`

    ordered_json whereJson(const resource::ScriptLocation& where) {
        return { { "file", where.file }, { "line", where.line } };
    }

    ordered_json findingsJson(const std::vector<ScriptFinding>& findings) {
        auto out = ordered_json::array();
        for (const ScriptFinding& f : findings) {
            ordered_json json{ { "severity", resource::severityName(f.severity) }, { "check", f.check }, { "message", f.message } };
            json.update(whereJson(f.where));
            if (!f.procedure.empty()) {
                json["procedure"] = f.procedure;
            }
            out.push_back(std::move(json));
        }
        return out;
    }

    void count(const std::vector<ScriptFinding>& findings, ordered_json& summary) {
        for (const ScriptFinding& f : findings) {
            const std::string key = std::string(resource::severityName(f.severity)) + "s";
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

    const std::vector<ScriptReport> parsed = resource::reportScripts(resources, paths);
    resource::ScriptLinter linter(resources, *scripts, options.locale);
    ordered_json summary = ordered_json::object();
    if (!options.all) {
        const std::vector<ScriptFinding> findings = linter.findings(parsed.front(), targets.front(), options.includeNotes);
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
        const std::vector<ScriptFinding> findings = linter.findings(parsed[i], targets[i], options.includeNotes);
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
    const ScriptReport r = resource::reportScripts(resources, { std::filesystem::path(source->second) }).front();
    resource::ScriptLinter linter(resources, scripts, locale);

    auto procedures = ordered_json::array();
    int includedLive = 0;
    for (const resource::ScriptProcedure& p : r.procedures) {
        if (!p.inScript) {
            includedLive += p.live ? 1 : 0;
            continue;
        }
        procedures.push_back({ { "name", p.name }, { "line", p.where.line }, { "parameters", p.parameters }, { "live", p.live },
            { "entryPoint", p.entryPoint } });
    }
    const auto variableJson = [&r](const resource::ScriptUse<ssl::VariableRef>& use) {
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
