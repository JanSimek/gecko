#include "resource/ScriptLint.h"

#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "format/gam/Gam.h"
#include "format/msg/Msg.h"
#include "resource/GameResources.h"
#include "resource/GameVarsLoader.h"
#include "resource/ScriptSourceLocator.h"
#include "resource/VfsSourceProvider.h"
#include "ssl/CallGraph.h"
#include "ssl/Corpus.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
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
    r.calls = uses(unit, ssl::callRefs(unit, graph, { "add_timer_event", "create_object_sid", "move_to", "critter_attempt_placement" }));
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

ScriptLinter::ScriptLinter(GameResources& resources, const Lst* scripts, std::string locale)
    : _resources(resources)
    , _scripts(scripts)
    , _locale(std::move(locale)) {}

std::vector<ScriptFinding> ScriptLinter::findings(const ScriptReport& report, int programIndex, bool includeNotes) {
    std::vector<ScriptFinding> out = report.syntax;
    localVariables(report, programIndex, out);
    messages(report, out);
    globalVariables(report, out);
    timers(report, out);
    objectsAndTiles(report, out);
    if (includeNotes) {
        unusedLocalVariables(report, out);
        for (const ScriptProcedure& procedure : report.procedures) {
            if (procedure.inScript && !procedure.live) {
                out.emplace_back(Severity::Note, "unused-procedure",
                    std::format("{} never runs: nothing calls it, so sslc leaves it out of the compiled script", procedure.name),
                    procedure.where, procedure.name);
            }
        }
    }
    return out;
}

int ScriptLinter::localVariableCount(int programIndex) const {
    if (_scripts == nullptr) {
        return 0;
    }
    const auto& counts = _scripts->localVars();
    return programIndex >= 0 && static_cast<std::size_t>(programIndex) < counts.size() ? counts[static_cast<std::size_t>(programIndex)] : 0;
}

void ScriptLinter::localVariables(const ScriptReport& report, int programIndex, std::vector<ScriptFinding>& out) const {
    if (_scripts == nullptr || programIndex < 0 || static_cast<std::size_t>(programIndex) >= _scripts->list().size()) {
        return;
    }
    const int count = localVariableCount(programIndex);
    const std::string script = scriptBaseName(_scripts->list()[static_cast<std::size_t>(programIndex)]);
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
        out.emplace_back(Severity::Error, "local-vars", message, use.where, use.ref.procedure);
    }
}

void ScriptLinter::messages(const ScriptReport& report, std::vector<ScriptFinding>& out) {
    if (_scripts == nullptr) {
        return;
    }
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
        if (list <= 0 || static_cast<std::size_t>(list) > _scripts->list().size()) {
            out.emplace_back(Severity::Error, "messages", std::format("{} reads message list {}, which names no scripts.lst line", use.ref.opcode, list),
                use.where, use.ref.procedure);
            continue;
        }
        const std::string name = scriptBaseName(_scripts->list()[static_cast<std::size_t>(list - 1)]);
        if (const Msg* dialog = dialogOf(name); dialog == nullptr) {
            if (missingFiles.insert(list).second) {
                out.emplace_back(Severity::Warning, "messages", std::format("dialog/{}.msg is not mounted, so its messages can't be checked", name),
                    use.where, use.ref.procedure);
            }
        } else if (!dialog->getMessages().contains(number)) {
            out.emplace_back(Severity::Error, "messages", std::format("message {} is not in dialog/{}.msg; the engine shows \"Error\" instead", number, name),
                use.where, use.ref.procedure);
        }
    }
}

namespace {

    struct LocalVarBlock {
        uint32_t sid;
        uint32_t programIndex;
        std::size_t begin;
        std::size_t end;
    };

    // Per script: whether its block runs past the array, and which scripts' blocks it overlaps (with
    // how many of each).
    struct BlockClash {
        bool pastEnd = false;
        std::map<std::string, int, std::less<>> others;
    };

    // Each placed script's block, sized as the engine sizes it, in offset order.
    std::vector<LocalVarBlock> placedBlocks(const Map::MapFile& mapFile, const Lst& scripts) {
        const bool saved = (mapFile.header.flags & static_cast<uint32_t>(MapHeaderFlags::Saved)) != 0;
        const auto& counts = scripts.localVars();
        std::vector<LocalVarBlock> blocks;
        for (const auto& section : mapFile.map_scripts) {
            for (const MapScript& script : section) {
                if (script.local_var_offset == MapScript::NONE) {
                    continue; // the engine gives it a fresh block on first use
                }
                const int listed = script.script_id < counts.size() ? counts[script.script_id] : 0;
                const int count = saved && script.local_var_count != 0 ? static_cast<int>(script.local_var_count) : listed;
                if (count > 0) {
                    blocks.push_back({ script.pid, script.script_id, script.local_var_offset,
                        std::size_t{ script.local_var_offset } + static_cast<std::size_t>(count) });
                }
            }
        }
        std::ranges::sort(blocks, {}, &LocalVarBlock::begin);
        return blocks;
    }

    std::map<uint32_t, BlockClash> clashes(const std::vector<LocalVarBlock>& blocks, std::size_t total, const Lst& scripts) {
        const auto nameOf = [&scripts](uint32_t programIndex) {
            return programIndex < scripts.list().size() ? scripts.list()[programIndex] : std::format("script #{}", programIndex);
        };
        std::map<uint32_t, BlockClash> found;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const LocalVarBlock& block = blocks[i];
            if (block.end > total) {
                found[block.sid].pastEnd = true;
            }
            for (std::size_t j = i + 1; j < blocks.size() && blocks[j].begin < block.end; ++j) {
                ++found[block.sid].others[nameOf(blocks[j].programIndex)];
                ++found[blocks[j].sid].others[nameOf(block.programIndex)];
            }
        }
        return found;
    }

    std::string describe(const LocalVarBlock& block, const BlockClash& clash, std::size_t total) {
        std::string text;
        if (clash.pastEnd) {
            text = std::format("its local variables {}-{} run past the map's {}, so those fail to read and write", block.begin, block.end - 1, total);
        }
        if (!clash.others.empty()) {
            std::string others;
            for (const auto& [name, times] : clash.others) {
                others += (others.empty() ? "" : ", ") + (times > 1 ? std::format("{} ({}x)", name, times) : name);
            }
            text += (text.empty() ? "" : "; ")
                + std::format("its local variables {}-{} overlap those of {}, so they share them", block.begin, block.end - 1, others);
        }
        text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())));
        return text + ".";
    }

} // namespace

std::map<uint32_t, std::string> localVarBlockProblems(const Map& map, const Lst& scripts) {
    const auto& mapFile = map.getMapFile();
    const std::vector<LocalVarBlock> blocks = placedBlocks(mapFile, scripts);
    const std::map<uint32_t, BlockClash> found = clashes(blocks, mapFile.map_local_vars.size(), scripts);
    std::map<uint32_t, std::string> problems;
    for (const LocalVarBlock& block : blocks) {
        if (const auto it = found.find(block.sid); it != found.end()) {
            problems.try_emplace(block.sid, describe(block, it->second, mapFile.map_local_vars.size()));
        }
    }
    return problems;
}

void ScriptLinter::globalVariables(const ScriptReport& report, std::vector<ScriptFinding>& out) {
    if (!_gamLoaded) {
        _gam = loadVault13Gam(_resources);
        _gamLoaded = true;
    }
    if (_gam == nullptr) {
        return;
    }
    const auto count = static_cast<int>(_gam->gvarCount());
    std::set<int> reported;
    for (const auto& use : report.globals) {
        const auto& index = use.ref.index;
        if (!index.has_value() || (*index >= 0 && *index < count) || !reported.insert(*index).second) {
            continue;
        }
        const std::string what = use.ref.name.empty() ? std::format("global variable {}", *index) : std::format("{} ({})", use.ref.name, *index);
        out.emplace_back(Severity::Error, "gvars",
            std::format("{} is not one of vault13.gam's {} global variables, so reading it gives 0 and setting it does nothing", what, count),
            use.where, use.ref.procedure);
    }
}

void ScriptLinter::timers(const ScriptReport& report, std::vector<ScriptFinding>& out) const {
    if (std::ranges::any_of(report.procedures, [](const ScriptProcedure& p) {
            return std::ranges::equal(p.name, std::string_view("timed_event_p_proc"),
                [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
        })) {
        return;
    }
    for (const auto& use : report.calls) {
        if (use.ref.opcode == "add_timer_event" && !use.ref.self.empty() && use.ref.self.front()) {
            out.emplace_back(Severity::Warning, "timers",
                "add_timer_event on self_obj, but the script has no timed_event_p_proc for the event to run, so it does nothing",
                use.where, use.ref.procedure);
            return; // once is enough
        }
    }
}

namespace {

    // fallout2-ce object.cc objectSetLocation: a hex outside the 200x200 grid or an elevation outside
    // 0-2 is refused, and the object stays where it was (or, just created, nowhere).
    constexpr int64_t HEX_COUNT = 200 * 200;

    bool badTile(const std::optional<int64_t>& tile) {
        return tile.has_value() && (*tile < 0 || *tile >= HEX_COUNT);
    }

    bool badElevation(const std::optional<int64_t>& elevation) {
        return elevation.has_value() && (*elevation < 0 || *elevation > 2);
    }

    void addError(std::vector<ScriptFinding>& out, const ScriptUse<ssl::CallRef>& use, const char* check, std::string message) {
        out.emplace_back(Severity::Error, check, std::move(message), use.where, use.ref.procedure);
    }

    /// move_to / critter_attempt_placement(obj, tile, elevation)
    void movedTo(const ScriptUse<ssl::CallRef>& use, std::vector<ScriptFinding>& out) {
        const auto& args = use.ref.args;
        if (badTile(args[1])) {
            addError(out, use, "tiles", std::format("{} to hex {}, outside 0-39999, so nothing moves", use.ref.opcode, *args[1]));
        }
        if (badElevation(args[2])) {
            addError(out, use, "tiles", std::format("{} to elevation {}, outside 0-2, so nothing moves", use.ref.opcode, *args[2]));
        }
    }

} // namespace

void ScriptLinter::createdObject(const ScriptUse<ssl::CallRef>& use, std::vector<ScriptFinding>& out) {
    const auto& args = use.ref.args;
    if (const auto& pid = args[0]; pid.has_value() && *pid == 0) {
        addError(out, use, "objects", "create_object_sid with PID 0 creates nothing");
    } else if (pid.has_value() && !protoExists(*pid)) {
        addError(out, use, "objects", std::format("create_object_sid: PID 0x{:08X} has no proto, so nothing is created", static_cast<uint32_t>(*pid)));
    }
    // The tile -1 means "hex 0" to the engine; the script number is the 1-based scripts.lst line.
    if (const auto& tile = args[1]; tile != -1 && badTile(tile)) {
        addError(out, use, "tiles", std::format("create_object_sid on hex {}, outside 0-39999, so the object is not placed", *tile));
    }
    if (badElevation(args[2])) {
        addError(out, use, "tiles", std::format("create_object_sid on elevation {}, outside 0-2, so the object is not placed", *args[2]));
    }
    const auto& sid = args[3];
    if (_scripts != nullptr && sid.has_value() && *sid != -1 && (*sid <= 0 || *sid > static_cast<int64_t>(_scripts->list().size()))) {
        addError(out, use, "objects",
            std::format("create_object_sid with script {}, which is not a scripts.lst line (1-{}; -1 for none)", *sid, _scripts->list().size()));
    }
}

void ScriptLinter::objectsAndTiles(const ScriptReport& report, std::vector<ScriptFinding>& out) {
    for (const auto& use : report.calls) {
        if (use.ref.opcode == "create_object_sid" && use.ref.args.size() == 4) {
            createdObject(use, out);
        } else if ((use.ref.opcode == "move_to" || use.ref.opcode == "critter_attempt_placement") && use.ref.args.size() == 3) {
            movedTo(use, out);
        }
    }
}

void ScriptLinter::unusedLocalVariables(const ScriptReport& report, std::vector<ScriptFinding>& out) const {
    std::map<int, std::pair<const ScriptUse<ssl::VariableRef>*, const ScriptUse<ssl::VariableRef>*>> uses; // first get, first set
    for (const auto& use : report.locals) {
        if (!use.ref.index.has_value()) {
            return; // a computed variable number could be any of them
        }
        auto& [get, set] = uses[*use.ref.index];
        auto& slot = use.ref.kind == ssl::VariableRef::Kind::Get ? get : set;
        if (slot == nullptr) {
            slot = &use;
        }
    }
    for (const auto& [index, pair] : uses) {
        const auto& [get, set] = pair;
        const ScriptUse<ssl::VariableRef>* use = get != nullptr ? get : set;
        const std::string what = use->ref.name.empty() ? std::format("Local variable {}", index) : std::format("{} ({})", use->ref.name, index);
        if (set == nullptr) {
            out.emplace_back(Severity::Note, "local-vars", std::format("{} is read but never set, so it is always 0", what), get->where, get->ref.procedure);
        } else if (get == nullptr) {
            out.emplace_back(Severity::Note, "local-vars", std::format("{} is set but never read", what), set->where, set->ref.procedure);
        }
    }
}

bool ScriptLinter::protoExists(int64_t pid) {
    if (const auto it = _protos.find(pid); it != _protos.end()) {
        return it->second;
    }
    bool exists = false;
    try {
        exists = _resources.loadPro(static_cast<uint32_t>(pid)) != nullptr;
    } catch (const std::exception& e) {
        spdlog::debug("lint: no proto for PID {:#x}: {}", pid, e.what());
    }
    _protos.try_emplace(pid, exists);
    return exists;
}

ScriptPlacement placementOf(MapScript::ScriptType type) {
    using enum MapScript::ScriptType;
    using enum ScriptPlacement;
    switch (type) {
        case SPATIAL:
            return Spatial;
        case TIMER:
            return Timer;
        case ITEM:
            return Item;
        case CRITTER:
            return Critter;
        default:
            return ScriptPlacement::Map; // not the Map class: a system script, like the map's own
    }
}

std::vector<ScriptFinding> placementFindings(const ScriptReport& report, ScriptPlacement placement) {
    std::vector<ScriptFinding> out;
    const auto defines = [&report](std::string_view name) {
        return std::ranges::find_if(report.procedures, [name](const ScriptProcedure& p) {
            return std::ranges::equal(p.name, name, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
        });
    };
    const ScriptLocation script{ report.script, 0 };
    if (placement == ScriptPlacement::Map && !report.locals.empty()) {
        const auto& use = report.locals.front();
        out.emplace_back(Severity::Error, "local-vars",
            "This is a system script (the map's own, or one in the System section), and the engine refuses local_var in those: every read gives -1", use.where, use.ref.procedure);
    }
    if (placement == ScriptPlacement::Spatial && defines("spatial_p_proc") == report.procedures.end()) {
        out.emplace_back(Severity::Warning, "spatial", "A spatial script without spatial_p_proc: stepping into its area does nothing", script, std::string());
    }
    if (placement != ScriptPlacement::Critter) {
        for (const char* handler : { "critter_p_proc", "combat_p_proc" }) {
            if (const auto it = defines(handler); it != report.procedures.end()) {
                out.emplace_back(Severity::Note, "handlers", std::format("{} never runs here: the engine calls it only for critter scripts", handler),
                    it->where, it->name);
            }
        }
    }
    return out;
}

NeverSetGlobals neverSetGlobals(const std::vector<ScriptReport>& reports, const Gam& gam) {
    NeverSetGlobals result;
    const auto vars = gam.gameGlobalVars();
    std::set<int> written;
    struct FirstRead {
        std::size_t report;
        const ScriptUse<ssl::VariableRef>* use;
        int readers;
    };
    std::map<int, FirstRead> read;
    for (std::size_t i = 0; i < reports.size(); ++i) {
        std::set<int> readHere;
        for (const auto& use : reports[i].globals) {
            if (!use.ref.index.has_value()) {
                result.dynamicWrites += use.ref.kind == ssl::VariableRef::Kind::Set ? 1 : 0;
                continue;
            }
            const int index = *use.ref.index;
            if (use.ref.kind == ssl::VariableRef::Kind::Set) {
                written.insert(index);
            } else if (readHere.insert(index).second) {
                auto [it, fresh] = read.try_emplace(index, i, &use, 0);
                ++it->second.readers;
            }
        }
    }
    for (const auto& [index, first] : read) {
        if (index < 0 || static_cast<std::size_t>(index) >= vars.size() || written.contains(index)) {
            continue; // out of range is its own error; written somewhere is fine
        }
        const auto& [name, start] = vars[static_cast<std::size_t>(index)];
        if (engineWritesGlobal(name)) {
            continue;
        }
        const std::string others = first.readers > 1 ? std::format(" (as do {} other scripts)", first.readers - 1) : std::string();
        result.findings[first.report].emplace_back(Severity::Warning, "gvars",
            std::format("{} ({}) is read here{}, but no script sets it, so it stays at its vault13.gam start value of {}", name, index, others, start),
            first.use->where, first.use->ref.procedure);
    }
    return result;
}

bool engineWritesGlobal(std::string_view gvarName) {
    static constexpr std::array<std::string_view, 10> WRITTEN{ "GVAR_LOAD_MAP_INDEX", "GVAR_TOWN_REP_ARROYO", "GVAR_NUKA_COLA_ADDICT",
        "GVAR_BUFF_OUT_ADDICT", "GVAR_MENTATS_ADDICT", "GVAR_PSYCHO_ADDICT", "GVAR_RADAWAY_ADDICT", "GVAR_ALCOHOL_ADDICT", "GVAR_ADDICT_JET",
        "GVAR_ADDICT_TRAGIC" };
    return std::ranges::find(WRITTEN, gvarName) != WRITTEN.end();
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
