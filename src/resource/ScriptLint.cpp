#include "resource/ScriptLint.h"

#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "format/msg/Msg.h"
#include "resource/GameResources.h"
#include "resource/ScriptSourceLocator.h"
#include "resource/VfsSourceProvider.h"
#include "ssl/CallGraph.h"
#include "ssl/Corpus.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <set>
#include <tuple>

namespace geck::resource {

namespace {

    using Severity = ScriptFinding::Severity;

    ScriptLocation locationOf(const ssl::ScriptUnit& unit, const ssl::SourceLocation& location) {
        return { location.file != 0 ? unit.sources().path(location.file).generic_string() : std::string(), location.line };
    }

    template <typename Ref>
    std::vector<ScriptUse<Ref>> uses(const ssl::ScriptUnit& unit, std::vector<Ref> refs) {
        std::vector<ScriptUse<Ref>> out;
        out.reserve(refs.size());
        for (Ref& ref : refs) {
            const ScriptLocation where = locationOf(unit, ref.location);
            out.push_back({ std::move(ref), where });
        }
        return out;
    }

} // namespace

const char* severityName(ScriptFinding::Severity severity) {
    switch (severity) {
        case Severity::Error:
            return "error";
        case Severity::Warning:
            return "warning";
        case Severity::Note:
            return "note";
    }
    return "error";
}

ScriptReport reportScript(const ssl::ScriptUnit& unit) {
    ScriptReport r;
    const auto scriptFile = unit.scriptFile();
    if (scriptFile.has_value()) {
        r.script = unit.sources().path(*scriptFile).generic_string();
    }
    for (const ssl::Diagnostic& d : unit.diagnostics()) {
        const Severity severity = d.severity == ssl::Diagnostic::Severity::Error ? Severity::Error : Severity::Warning;
        r.syntax.push_back({ severity, "syntax", d.message, locationOf(unit, d.location), {} });
    }
    const ssl::CallGraph graph(unit.program());
    for (const ssl::Procedure& procedure : unit.program().procedures) {
        if (!procedure.body.has_value()) {
            continue;
        }
        const ssl::SourceLocation written = unit.writtenAt(procedure.range);
        r.procedures.push_back({ procedure.name, graph.isLive(procedure.name), ssl::isEngineProcedure(procedure.name),
            procedure.parameters.size(), locationOf(unit, written), scriptFile.has_value() && written.file == *scriptFile });
    }
    r.globals = uses(unit, ssl::globalVariableRefs(unit, graph));
    r.locals = uses(unit, ssl::localVariableRefs(unit, graph));
    r.messages = uses(unit, ssl::messageRefs(unit, graph));
    ssl::ScriptFacts facts = ssl::readFacts(unit);
    r.overrides = std::move(facts.overrides);
    r.localVarNames = std::move(facts.localVarNames);
    return r;
}

std::vector<ScriptReport> reportScripts(GameResources& resources, const std::vector<std::filesystem::path>& paths) {
    std::vector<ScriptReport> out(paths.size());
    const VfsSourceProvider provider(resources.files());
    ssl::loadScripts(provider, paths, [&out](std::size_t index, const ssl::ScriptUnit& unit) { out[index] = reportScript(unit); });
    return out;
}

ScriptLinter::ScriptLinter(GameResources& resources, const Lst& scripts, std::string locale)
    : _resources(resources)
    , _scripts(scripts)
    , _locale(std::move(locale)) {}

std::vector<ScriptFinding> ScriptLinter::findings(const ScriptReport& report, int programIndex, bool includeNotes) {
    std::vector<ScriptFinding> out = report.syntax;
    localVariables(report, programIndex, out);
    messages(report, out);
    if (includeNotes) {
        for (const ScriptProcedure& procedure : report.procedures) {
            if (procedure.inScript && !procedure.live) {
                out.push_back({ Severity::Note, "unused-procedure",
                    std::format("{} never runs: nothing calls it, so sslc leaves it out of the compiled script", procedure.name),
                    procedure.where, procedure.name });
            }
        }
    }
    return out;
}

int ScriptLinter::localVariableCount(int programIndex) const {
    const auto& counts = _scripts.localVars();
    return programIndex >= 0 && static_cast<std::size_t>(programIndex) < counts.size() ? counts[static_cast<std::size_t>(programIndex)] : 0;
}

void ScriptLinter::localVariables(const ScriptReport& report, int programIndex, std::vector<ScriptFinding>& out) const {
    if (programIndex < 0 || static_cast<std::size_t>(programIndex) >= _scripts.list().size()) {
        return;
    }
    const int count = localVariableCount(programIndex);
    const std::string script = scriptBaseName(_scripts.list()[static_cast<std::size_t>(programIndex)]);
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
        out.push_back({ Severity::Error, "local-vars", message, use.where, use.ref.procedure });
    }
}

void ScriptLinter::messages(const ScriptReport& report, std::vector<ScriptFinding>& out) {
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
            out.push_back({ Severity::Error, "messages", std::format("{} reads message list {}, which names no scripts.lst line", use.ref.opcode, list),
                use.where, use.ref.procedure });
            continue;
        }
        const std::string name = scriptBaseName(_scripts.list()[static_cast<std::size_t>(list - 1)]);
        if (const Msg* dialog = dialogOf(name); dialog == nullptr) {
            if (missingFiles.insert(list).second) {
                out.push_back({ Severity::Warning, "messages", std::format("dialog/{}.msg is not mounted, so its messages can't be checked", name),
                    use.where, use.ref.procedure });
            }
        } else if (!dialog->getMessages().contains(number)) {
            out.push_back({ Severity::Error, "messages", std::format("message {} is not in dialog/{}.msg; the engine shows \"Error\" instead", number, name),
                use.where, use.ref.procedure });
        }
    }
}

std::map<uint32_t, std::string> localVarBlockProblems(const Map& map, const Lst& scripts) {
    const auto& mapFile = map.getMapFile();
    const bool saved = (mapFile.header.flags & static_cast<uint32_t>(MapHeaderFlags::Saved)) != 0;
    const auto& counts = scripts.localVars();
    const auto nameOf = [&scripts](uint32_t programIndex) {
        return programIndex < scripts.list().size() ? scripts.list()[programIndex] : std::format("script #{}", programIndex);
    };

    struct Block {
        uint32_t sid;
        uint32_t programIndex;
        std::size_t begin;
        std::size_t end;
    };
    std::vector<Block> blocks;
    for (const auto& section : mapFile.map_scripts) {
        for (const MapScript& script : section) {
            if (script.local_var_offset == MapScript::NONE) {
                continue; // the engine gives it a fresh block on first use
            }
            const int listed = script.script_id < counts.size() ? counts[script.script_id] : 0;
            const int count = saved && script.local_var_count != 0 ? static_cast<int>(script.local_var_count) : listed;
            if (count > 0) {
                blocks.push_back({ script.pid, script.script_id, script.local_var_offset, std::size_t{ script.local_var_offset } + static_cast<std::size_t>(count) });
            }
        }
    }
    std::ranges::sort(blocks, {}, &Block::begin);

    // Per script: whether its block runs past the array, and which scripts' blocks it overlaps (with
    // how many of each).
    struct Overlap {
        bool pastEnd = false;
        std::map<std::string, int, std::less<>> others;
    };
    std::map<uint32_t, Overlap> found;
    const std::size_t total = mapFile.map_local_vars.size();
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const Block& block = blocks[i];
        if (block.end > total) {
            found[block.sid].pastEnd = true;
        }
        for (std::size_t j = i + 1; j < blocks.size() && blocks[j].begin < block.end; ++j) {
            ++found[block.sid].others[nameOf(blocks[j].programIndex)];
            ++found[blocks[j].sid].others[nameOf(block.programIndex)];
        }
    }

    std::map<uint32_t, std::string> problems;
    for (const Block& block : blocks) {
        const auto it = found.find(block.sid);
        if (it == found.end()) {
            continue;
        }
        std::vector<std::string> parts;
        if (it->second.pastEnd) {
            parts.push_back(std::format("its local variables {}-{} run past the map's {}, so those fail to read and write", block.begin,
                block.end - 1, total));
        }
        if (!it->second.others.empty()) {
            std::string others;
            for (const auto& [name, times] : it->second.others) {
                others += (others.empty() ? "" : ", ") + (times > 1 ? std::format("{} ({}x)", name, times) : name);
            }
            parts.push_back(std::format("its local variables {}-{} overlap those of {}, so they share them", block.begin, block.end - 1, others));
        }
        std::string text;
        for (const auto& part : parts) {
            text += (text.empty() ? "" : "; ") + part;
        }
        text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())));
        problems.try_emplace(block.sid, text + ".");
    }
    return problems;
}

const Msg* ScriptLinter::dialogOf(const std::string& name) {
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

} // namespace geck::resource
