#include "ssl/ScriptFacts.h"

#include <algorithm>
#include <cctype>
#include <format>

#include "ssl/AstWalk.h"

namespace geck::ssl {

namespace {

    // fallout2-ce interpreter_extra.cc: CRITTER_TRAIT_OBJECT and its AI packet / team selectors.
    constexpr int64_t TRAIT_OBJECT = 1;
    constexpr int64_t TRAIT_OBJECT_AI_PACKET = 5;
    constexpr int64_t TRAIT_OBJECT_TEAM = 6;

    using Field = ScriptFacts::Override::Field;

    // SSL names are case-insensitive (sslc compares them with _stricmp).
    bool sameName(std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    bool isName(const ExprPtr& expr, std::string_view name) {
        const auto* node = expr ? std::get_if<Name>(&expr->node) : nullptr;
        return node != nullptr && sameName(node->name, name);
    }

    class FactsReader {
    public:
        explicit FactsReader(const ScriptUnit& unit)
            : _unit(unit) {}

        std::vector<ScriptFacts::Override> overrides() const {
            std::vector<ScriptFacts::Override> found;
            for (const Procedure& procedure : _unit.program().procedures) {
                if (!procedure.body.has_value()) {
                    continue;
                }
                forEachExpression(*procedure.body, [&](const Expr& expr) {
                    const auto* call = std::get_if<Call>(&expr.node);
                    if (auto write = call ? selfTraitWrite(expr, *call) : std::nullopt; write.has_value()) {
                        write->procedure = procedure.name;
                        found.push_back(std::move(*write));
                    }
                });
            }
            return found;
        }

    private:
        // A call that assigns the AI packet or team of the script's own object. Normally that is
        // critter_add_trait after expansion; the command.h aliases are recognised by name only when they
        // survived preprocessing undefined (command.h not found), so a missing header still reports.
        std::optional<ScriptFacts::Override> selfTraitWrite(const Expr& expr, const Call& call) const {
            const auto& args = call.args;
            std::optional<Field> field;
            const Expr* value = nullptr;
            if (sameName(call.callee, "critter_add_trait") && args.size() == 4 && isName(args[0], "self_obj")
                && constant(*args[1]) == TRAIT_OBJECT) {
                if (const auto selector = constant(*args[2]); selector == TRAIT_OBJECT_AI_PACKET) {
                    field = Field::AiPacket;
                } else if (selector == TRAIT_OBJECT_TEAM) {
                    field = Field::Team;
                }
                value = args[3].get();
            } else if ((sameName(call.callee, "set_self_ai") || sameName(call.callee, "set_self_team")) && args.size() == 1) {
                field = sameName(call.callee, "set_self_ai") ? Field::AiPacket : Field::Team;
                value = args[0].get();
            } else if ((sameName(call.callee, "set_ai") || sameName(call.callee, "set_team")) && args.size() == 2
                && isName(args[0], "self_obj")) {
                field = sameName(call.callee, "set_ai") ? Field::AiPacket : Field::Team;
                value = args[1].get();
            }
            if (!field.has_value() || value == nullptr) {
                return std::nullopt;
            }

            ScriptFacts::Override write;
            write.field = *field;
            const SourceLocation written = _unit.writtenAt(expr.range);
            write.file = _unit.sources().path(written.file);
            write.line = static_cast<int>(written.line);
            if (const auto number = _unit.evaluate(*value); number.has_value()) {
                write.value = static_cast<int>(*number);
            }
            write.valueName = valueMacro(expr, *value);
            return write;
        }

        // A trait selector's value; by name when define.h was not found to define it.
        std::optional<int64_t> constant(const Expr& arg) const {
            if (auto value = _unit.evaluate(arg); value.has_value()) {
                return value;
            }
            if (const auto* name = std::get_if<Name>(&arg.node)) {
                if (sameName(name->name, "TRAIT_OBJECT")) {
                    return TRAIT_OBJECT;
                }
                if (sameName(name->name, "OBJECT_AI_PACKET")) {
                    return TRAIT_OBJECT_AI_PACKET;
                }
                if (sameName(name->name, "OBJECT_TEAM_NUM")) {
                    return TRAIT_OBJECT_TEAM;
                }
            }
            return std::nullopt;
        }

        // The object-like macro the value was written as: the outermost macro the value came out of
        // that the call itself did not (set_self_ai, set_ai, ...).
        std::string valueMacro(const Expr& call, const Expr& value) const {
            const auto callChain = _unit.macroChain(call.range);
            for (const Macro* macro : _unit.macroChain(value.range)) {
                if (!macro->functionLike && std::ranges::find(callChain, macro) == callChain.end()) {
                    return std::string(macro->name);
                }
            }
            return {};
        }

        const ScriptUnit& _unit;
    };

    std::map<int, std::string> localVarNames(const ScriptUnit& unit) {
        std::map<int, std::string> fromHeaders;
        std::map<int, std::string> fromScript;
        for (const Macro& macro : unit.preprocessor().definitions()) {
            if (macro.functionLike || !macro.name.starts_with("LVAR_")) {
                continue;
            }
            if (const auto index = unit.evaluate(macro); index.has_value()) {
                auto& names = macro.location.file == unit.scriptFile() ? fromScript : fromHeaders;
                names.insert_or_assign(static_cast<int>(*index), std::string(macro.name));
            }
        }
        fromScript.merge(fromHeaders); // keeps the script's name where both have an index
        return fromScript;
    }

} // namespace

bool ScriptFacts::assigns(Override::Field field) const {
    return std::ranges::any_of(overrides, [field](const Override& o) { return o.field == field; });
}

ScriptFacts readFacts(const ScriptUnit& unit) {
    ScriptFacts facts;
    if (const auto script = unit.scriptFile(); script.has_value()) {
        facts.script = unit.sources().path(*script);
    }
    facts.overrides = FactsReader(unit).overrides();
    facts.localVarNames = localVarNames(unit);
    for (const Diagnostic& d : unit.diagnostics()) {
        const std::string file = d.location.file != 0 ? unit.sources().path(d.location.file).generic_string() : facts.script.generic_string();
        facts.problems.push_back(std::format("{}:{}: {}", file, d.location.line, d.message));
    }
    return facts;
}

ScriptFacts inspectScript(const SourceProvider& provider, const std::filesystem::path& path, const PreprocessOptions& options) {
    return readFacts(ScriptUnit::read(provider, path, options));
}

} // namespace geck::ssl
