#include "cli/ScriptLint.h"

#include "cli/ScriptLookup.h"
#include "resource/ResourcePaths.h"
#include "format/map/MapScript.h"
#include "format/map/Map.h"
#include "cli/MapLoad.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "format/gam/Gam.h"
#include "resource/GameVarsLoader.h"
#include "resource/ScriptLint.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <tuple>
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

namespace {

    const char* placementName(resource::ScriptPlacement placement) {
        using enum resource::ScriptPlacement;
        switch (placement) {
            case Map:
                return "map";
            case Spatial:
                return "spatial";
            case Timer:
                return "timer";
            case Critter:
                return "critter";
            case Item:
                break;
        }
        return "item";
    }

    // Where a map places each program, and the SIDs of its placed records.
    struct MapPlacements {
        std::map<int, std::set<resource::ScriptPlacement>> placements;
        std::map<int, std::vector<uint32_t>> sids;
    };

    MapPlacements placementsOf(const Map& map) {
        MapPlacements out;
        const auto& mapFile = map.getMapFile();
        if (mapFile.header.script_id > 0) {
            out.placements[mapFile.header.script_id - 1].insert(resource::ScriptPlacement::Map);
        }
        for (std::size_t section = 0; section < std::size(mapFile.map_scripts); ++section) {
            for (const MapScript& script : mapFile.map_scripts[section]) {
                out.placements[static_cast<int>(script.script_id)].insert(resource::placementOf(static_cast<MapScript::ScriptType>(section)));
                out.sids[static_cast<int>(script.script_id)].push_back(script.pid);
            }
        }
        return out;
    }

    // One placed script's findings: its own, then for where it is placed and its local variable block.
    std::vector<ScriptFinding> placedFindings(resource::ScriptLinter& linter, const ScriptReport& report, int index, const MapPlacements& map,
        const std::map<uint32_t, std::string>& blocks, bool includeNotes) {
        std::vector<ScriptFinding> findings = linter.findings(report, index, includeNotes);
        for (const auto placement : map.placements.at(index)) {
            for (auto& finding : resource::placementFindings(report, placement)) {
                if (finding.severity != ScriptFinding::Severity::Note || includeNotes) {
                    findings.push_back(std::move(finding));
                }
            }
        }
        const auto sids = map.sids.find(index);
        if (report.locals.empty() || sids == map.sids.end()) {
            return findings;
        }
        for (const uint32_t sid : sids->second) {
            if (const auto it = blocks.find(sid); it != blocks.end()) {
                findings.push_back({ ScriptFinding::Severity::Error, "local-vars", it->second, { report.script, 0 }, {} });
                break; // the records of one script say the same thing
            }
        }
        return findings;
    }

    // `map lint-script --map`: every script the map places, with the placement and local variable
    // block checks on top of each script's own.
    int lintMap(resource::GameResources& resources, const Lst& scripts, const LintOptions& options, std::ostream& out) {
        std::unique_ptr<Map> map;
        try {
            map = loadMap(resources, options.map);
        } catch (const std::exception& e) {
            out << "lint-script: cannot load " << options.map << ": " << e.what() << "\n";
            return 1;
        }
        if (map == nullptr) {
            out << "lint-script: cannot load " << options.map << "\n";
            return 1;
        }
        const MapPlacements placed = placementsOf(*map);
        const auto sources = sourcesByIndex(resources, scripts);
        std::vector<int> targets;
        std::vector<std::filesystem::path> paths;
        auto unchecked = ordered_json::array();
        for (const auto& [index, where] : placed.placements) {
            if (const auto it = sources.find(index); it != sources.end()) {
                targets.push_back(index);
                paths.emplace_back(it->second);
            } else if (index >= 0 && static_cast<std::size_t>(index) < scripts.list().size()) {
                unchecked.push_back(stem(scripts.list()[static_cast<std::size_t>(index)]));
            }
        }
        const std::vector<ScriptReport> parsed = resource::reportScripts(resources, paths);
        resource::ScriptLinter linter(resources, &scripts, options.locale);
        const auto blocks = resource::localVarBlockProblems(*map, scripts);

        ordered_json summary = ordered_json::object();
        auto results = ordered_json::array();
        for (std::size_t i = 0; i < parsed.size(); ++i) {
            const int index = targets[i];
            const std::vector<ScriptFinding> findings = placedFindings(linter, parsed[i], index, placed, blocks, options.includeNotes);
            count(findings, summary);
            if (findings.empty()) {
                continue;
            }
            auto where = ordered_json::array();
            for (const auto placement : placed.placements.at(index)) {
                where.push_back(placementName(placement));
            }
            results.push_back({ { "name", stem(scripts.list()[static_cast<std::size_t>(index)]) }, { "programIndex", index },
                { "placed", std::move(where) }, { "findings", findingsJson(findings) } });
        }
        ordered_json root;
        root["map"] = options.map;
        root["scripts"] = parsed.size();
        root["withoutSource"] = std::move(unchecked);
        root["summary"] = summary;
        root["results"] = std::move(results);
        emit(out, root);
        return 0;
    }

} // namespace

int lintScripts(resource::GameResources& resources, const LintOptions& options, std::ostream& out) {
    const Lst* scripts = loadScriptsLst(resources);
    if (scripts == nullptr) {
        out << "lint-script: scripts.lst not found (is the Fallout 2 data mounted?)\n";
        return 1;
    }
    if (!options.map.empty()) {
        return lintMap(resources, *scripts, options, out);
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
    resource::ScriptLinter linter(resources, scripts, options.locale);
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

    // Across every script: global variables that are read but that nothing sets.
    resource::NeverSetGlobals neverSet;
    if (const Gam* gam = resource::loadVault13Gam(resources); gam != nullptr) {
        neverSet = resource::neverSetGlobals(parsed, *gam);
    }

    auto results = ordered_json::array();
    std::size_t total = 0;
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        std::vector<ScriptFinding> findings = linter.findings(parsed[i], targets[i], options.includeNotes);
        if (const auto it = neverSet.findings.find(i); it != neverSet.findings.end()) {
            findings.insert(findings.end(), it->second.begin(), it->second.end());
        }
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
    // A set_global_var with a computed number could set any gvar, so "never set" may miss those.
    root["computedGlobalWrites"] = neverSet.dynamicWrites;
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
    resource::ScriptLinter linter(resources, &scripts, locale);

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

int lintFiles(resource::GameResources& resources, const std::vector<std::filesystem::path>& files, bool includeNotes,
    const std::string& locale, bool json, std::ostream& out) {
    const Lst* scripts = nullptr;
    try {
        scripts = resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
    } catch (const std::exception& e) {
        spdlog::debug("lint: no scripts.lst ({}), so the checks that need it are skipped", e.what());
    }
    resource::ScriptLinter linter(resources, scripts, locale);
    const ssl::DiskSourceProvider provider;

    bool anyError = false;
    auto results = ordered_json::array();
    for (const auto& file : files) {
        // scripts.lst names the compiled script, so the source's base name finds its line.
        int programIndex = -1;
        if (scripts != nullptr) {
            const std::string base = toLower(stem(file.string()));
            const auto it = std::ranges::find_if(scripts->list(), [&base](const std::string& entry) { return toLower(stem(entry)) == base; });
            programIndex = it == scripts->list().end() ? -1 : static_cast<int>(it - scripts->list().begin());
        }
        const ScriptReport report = resource::reportScript(ssl::ScriptUnit::load(provider, file));
        std::vector<ScriptFinding> findings = linter.findings(report, programIndex, includeNotes);
        std::ranges::stable_sort(findings, [](const ScriptFinding& a, const ScriptFinding& b) {
            return std::tie(a.where.file, a.where.line) < std::tie(b.where.file, b.where.line);
        });
        anyError = anyError || std::ranges::any_of(findings, [](const ScriptFinding& f) { return f.severity == ScriptFinding::Severity::Error; });
        if (json) {
            results.push_back({ { "file", file.generic_string() }, { "programIndex", programIndex }, { "findings", findingsJson(findings) } });
            continue;
        }
        for (const ScriptFinding& f : findings) {
            out << (f.where.file.empty() ? file.generic_string() : f.where.file) << ":" << f.where.line << ": "
                << resource::severityName(f.severity) << ": " << f.message << " [" << f.check << "]\n";
        }
    }
    if (json) {
        emit(out, ordered_json{ { "results", std::move(results) } });
    }
    return anyError ? 1 : 0;
}

} // namespace geck::cli
