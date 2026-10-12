#include "ssl/ScriptFacts.h"

#include <algorithm>
#include <cctype>

namespace geck::ssl {

namespace {

    // fallout2-ce interpreter_extra.cc: CRITTER_TRAIT_OBJECT and its AI packet / team selectors.
    constexpr int64_t TRAIT_OBJECT = 1;
    constexpr int64_t TRAIT_OBJECT_AI_PACKET = 5;
    constexpr int64_t TRAIT_OBJECT_TEAM = 6;

    // SSL keywords and opcode names are case-insensitive (sslc compares them with _stricmp).
    bool is(const Token& token, std::string_view word) {
        return token.kind == TokenKind::Identifier
            && std::ranges::equal(token.text, word, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    }

    using Args = std::vector<std::vector<Token>>;

    // The arguments of the call whose '(' is at `open`, split at top-level commas; `end` is set past ')'.
    std::optional<Args> callArgs(const std::vector<Token>& tokens, std::size_t open, std::size_t& end) {
        if (open >= tokens.size() || !tokens[open].is("(")) {
            return std::nullopt;
        }
        Args args(1);
        int depth = 0;
        for (std::size_t i = open; i < tokens.size(); ++i) {
            const Token& token = tokens[i];
            if (token.is("(") && depth++ == 0) {
                continue;
            }
            if (token.is(")") && --depth == 0) {
                end = i + 1;
                return args;
            }
            if (token.is(",") && depth == 1) {
                args.emplace_back();
            } else {
                args.back().push_back(token);
            }
        }
        return std::nullopt;
    }

    // Tracks which procedure body a token stream position is in: `procedure name [(params)] begin`
    // opens one; the matching `end` closes it. A forward declaration ends at ';' instead.
    class ProcedureTracker {
    public:
        explicit ProcedureTracker(const std::vector<Token>& tokens)
            : _tokens(tokens) {}

        void step(std::size_t i) {
            const Token& token = _tokens[i];
            if (_depth == 0 && is(token, "procedure") && i + 1 < _tokens.size()) {
                if (const auto begin = bodyStart(i + 2)) {
                    _name = std::string(_tokens[i + 1].text);
                    _bodyAt = *begin;
                }
                return;
            }
            if (i == _bodyAt || (_depth > 0 && is(token, "begin"))) {
                ++_depth;
            } else if (_depth > 0 && is(token, "end") && --_depth == 0) {
                _name.clear();
            }
        }

        const std::string& current() const { return _name; }

    private:
        std::optional<std::size_t> bodyStart(std::size_t from) const {
            for (std::size_t i = from; i < _tokens.size(); ++i) {
                if (_tokens[i].is(";")) {
                    return std::nullopt;
                }
                if (is(_tokens[i], "begin")) {
                    return i;
                }
            }
            return std::nullopt;
        }

        const std::vector<Token>& _tokens;
        std::string _name;
        std::size_t _bodyAt = static_cast<std::size_t>(-1);
        int _depth = 0;
    };

    class FactsReader {
    public:
        FactsReader(Preprocessor& preprocessor, const std::vector<Token>& tokens)
            : _pp(preprocessor)
            , _tokens(tokens) {}

        std::vector<ScriptFacts::Override> overrides() {
            std::vector<ScriptFacts::Override> found;
            ProcedureTracker procedures(_tokens);
            for (std::size_t i = 0; i < _tokens.size(); ++i) {
                procedures.step(i);
                if (procedures.current().empty() || _tokens[i].kind != TokenKind::Identifier) {
                    continue;
                }
                if (auto override = selfTraitWrite(i)) {
                    override->procedure = procedures.current();
                    found.push_back(std::move(*override));
                }
            }
            return found;
        }

    private:
        // A call at `i` that assigns the AI packet or team of the script's own object. Normally that is
        // critter_add_trait after expansion; the command.h aliases are recognised by name only when they
        // survived preprocessing undefined (command.h not found), so a missing header still reports.
        std::optional<ScriptFacts::Override> selfTraitWrite(std::size_t i) {
            const Token& call = _tokens[i];
            std::size_t end = 0;
            const auto args = callArgs(_tokens, i + 1, end);
            if (!args) {
                return std::nullopt;
            }
            std::optional<ScriptFacts::Override::Field> field;
            const std::vector<Token>* value = nullptr;
            if (is(call, "critter_add_trait") && args->size() == 4 && isSelf((*args)[0])
                && constant((*args)[1]) == TRAIT_OBJECT) {
                const auto selector = constant((*args)[2]);
                if (selector == TRAIT_OBJECT_AI_PACKET) {
                    field = ScriptFacts::Override::Field::AiPacket;
                } else if (selector == TRAIT_OBJECT_TEAM) {
                    field = ScriptFacts::Override::Field::Team;
                }
                value = &(*args)[3];
            } else if ((is(call, "set_self_ai") || is(call, "set_self_team")) && args->size() == 1) {
                field = is(call, "set_self_ai") ? ScriptFacts::Override::Field::AiPacket : ScriptFacts::Override::Field::Team;
                value = &(*args)[0];
            } else if ((is(call, "set_ai") || is(call, "set_team")) && args->size() == 2 && isSelf((*args)[0])) {
                field = is(call, "set_ai") ? ScriptFacts::Override::Field::AiPacket : ScriptFacts::Override::Field::Team;
                value = &(*args)[1];
            }
            if (!field) {
                return std::nullopt;
            }

            ScriptFacts::Override override;
            override.field = *field;
            const SourceLocation written = _pp.writtenAt(call);
            override.file = _pp.sources().path(written.file);
            override.line = static_cast<int>(written.line);
            if (const auto number = _pp.evaluate(*value)) {
                override.value = static_cast<int>(*number);
            }
            override.valueName = valueMacro(call, *value);
            return override;
        }

        static bool isSelf(const std::vector<Token>& arg) {
            return arg.size() == 1 && is(arg.front(), "self_obj");
        }

        // A trait selector's value; by name when define.h was not found to define it.
        std::optional<int64_t> constant(const std::vector<Token>& arg) {
            if (auto value = _pp.evaluate(arg)) {
                return value;
            }
            if (arg.size() == 1) {
                if (is(arg.front(), "TRAIT_OBJECT")) {
                    return TRAIT_OBJECT;
                }
                if (is(arg.front(), "OBJECT_AI_PACKET")) {
                    return TRAIT_OBJECT_AI_PACKET;
                }
                if (is(arg.front(), "OBJECT_TEAM_NUM")) {
                    return TRAIT_OBJECT_TEAM;
                }
            }
            return std::nullopt;
        }

        // The object-like macro the value was written as: the outermost macro the value's tokens came
        // out of that is not one the call itself came out of (set_self_ai, set_ai, ...).
        std::string valueMacro(const Token& call, const std::vector<Token>& value) const {
            if (value.empty()) {
                return {};
            }
            const auto callChain = _pp.macroChain(call);
            for (const Macro* macro : _pp.macroChain(value.front())) {
                if (!macro->functionLike && std::ranges::find(callChain, macro) == callChain.end()) {
                    return std::string(macro->name);
                }
            }
            return {};
        }

        Preprocessor& _pp;
        const std::vector<Token>& _tokens;
    };

    std::map<int, std::string> localVarNames(Preprocessor& pp, FileId script) {
        std::map<int, std::string> fromHeaders;
        std::map<int, std::string> fromScript;
        for (const Macro& macro : pp.definitions()) {
            if (macro.functionLike || !macro.name.starts_with("LVAR_")) {
                continue;
            }
            if (const auto index = pp.evaluate(macro)) {
                auto& names = macro.location.file == script ? fromScript : fromHeaders;
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

ScriptFacts inspectScript(const SourceProvider& provider, const std::filesystem::path& path, const PreprocessOptions& options) {
    SourceManager sources(provider);
    Preprocessor pp(sources, options);
    const std::vector<Token> tokens = pp.run(path);

    ScriptFacts facts;
    facts.overrides = FactsReader(pp, tokens).overrides();
    if (const auto script = sources.load(path)) {
        facts.script = sources.path(*script);
        facts.localVarNames = localVarNames(pp, *script);
    }
    for (const Diagnostic& d : pp.diagnostics()) {
        const std::string file = d.location.file != 0 ? sources.path(d.location.file).generic_string() : path.generic_string();
        facts.problems.push_back(file + ":" + std::to_string(d.location.line) + ": " + d.message);
    }
    return facts;
}

} // namespace geck::ssl
