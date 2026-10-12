#include "ssl/Preprocessor.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <utility>

namespace geck::ssl {

namespace {

    constexpr std::string_view VA_ARGS = "__VA_ARGS__";

    Token endOfFile() {
        return Token{};
    }

    Token synthesizedNumber(int64_t value, const Token& like) {
        Token token = like;
        token.kind = TokenKind::Number;
        token.text = value != 0 ? "1" : "0";
        return token;
    }

    std::optional<int64_t> parseInteger(std::string_view text) {
        while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) {
            text.remove_suffix(1);
        }
        int base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            text.remove_prefix(2);
        } else if (text.size() > 1 && text[0] == '0') {
            base = 8;
            text.remove_prefix(1);
        }
        int64_t value = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (ec != std::errc() || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    /// An integer constant expression over already-expanded tokens, with C precedence.
    class ExpressionEvaluator {
    public:
        ExpressionEvaluator(const std::vector<Token>& tokens, bool identifiersAreZero)
            : _tokens(tokens)
            , _identifiersAreZero(identifiersAreZero) {}

        std::optional<int64_t> run() {
            auto value = conditional();
            if (!value || _pos != _tokens.size() || _failed) {
                return std::nullopt;
            }
            return value;
        }

    private:
        using Value = std::optional<int64_t>;

        const Token* peek() const { return _pos < _tokens.size() ? &_tokens[_pos] : nullptr; }

        bool accept(std::string_view punct) {
            if (const Token* token = peek(); token && token->is(punct)) {
                ++_pos;
                return true;
            }
            return false;
        }

        Value conditional() {
            Value condition = binary(0);
            if (!condition || !accept("?")) {
                return condition;
            }
            Value whenTrue = conditional();
            if (!whenTrue || !accept(":")) {
                return std::nullopt;
            }
            Value whenFalse = conditional();
            if (!whenFalse) {
                return std::nullopt;
            }
            return *condition != 0 ? whenTrue : whenFalse;
        }

        // Binary operators by precedence level, loosest first.
        static int precedence(std::string_view op) {
            static const std::map<std::string_view, int> levels{ { "||", 0 }, { "&&", 1 }, { "|", 2 }, { "^", 3 },
                { "&", 4 }, { "==", 5 }, { "!=", 5 }, { "<", 6 }, { ">", 6 }, { "<=", 6 }, { ">=", 6 }, { "<<", 7 },
                { ">>", 7 }, { "+", 8 }, { "-", 8 }, { "*", 9 }, { "/", 9 }, { "%", 9 } };
            const auto it = levels.find(op);
            return it == levels.end() ? -1 : it->second;
        }

        Value binary(int minLevel) {
            Value left = unary();
            while (left) {
                const Token* token = peek();
                const int level = token && token->kind == TokenKind::Punct ? precedence(token->text) : -1;
                if (level < minLevel) {
                    break;
                }
                ++_pos;
                Value right = binary(level + 1);
                if (!right) {
                    return std::nullopt;
                }
                left = apply(token->text, *left, *right);
            }
            return left;
        }

        Value apply(std::string_view op, int64_t a, int64_t b) {
            if ((op == "/" || op == "%") && b == 0) {
                _failed = true;
                return std::nullopt;
            }
            if (op == "||")
                return (a != 0 || b != 0) ? 1 : 0;
            if (op == "&&")
                return (a != 0 && b != 0) ? 1 : 0;
            if (op == "|")
                return a | b;
            if (op == "^")
                return a ^ b;
            if (op == "&")
                return a & b;
            if (op == "==")
                return a == b ? 1 : 0;
            if (op == "!=")
                return a != b ? 1 : 0;
            if (op == "<")
                return a < b ? 1 : 0;
            if (op == ">")
                return a > b ? 1 : 0;
            if (op == "<=")
                return a <= b ? 1 : 0;
            if (op == ">=")
                return a >= b ? 1 : 0;
            if (op == "<<")
                return a << (b & 63);
            if (op == ">>")
                return a >> (b & 63);
            if (op == "+")
                return a + b;
            if (op == "-")
                return a - b;
            if (op == "*")
                return a * b;
            if (op == "/")
                return a / b;
            return a % b;
        }

        Value unary() {
            if (accept("!")) {
                Value v = unary();
                return v ? Value(*v == 0 ? 1 : 0) : std::nullopt;
            }
            if (accept("~")) {
                Value v = unary();
                return v ? Value(~*v) : std::nullopt;
            }
            if (accept("-")) {
                Value v = unary();
                return v ? Value(-*v) : std::nullopt;
            }
            if (accept("+")) {
                return unary();
            }
            return primary();
        }

        Value primary() {
            const Token* token = peek();
            if (!token) {
                return std::nullopt;
            }
            if (accept("(")) {
                Value v = conditional();
                return v && accept(")") ? v : std::nullopt;
            }
            ++_pos;
            if (token->kind == TokenKind::Number) {
                return parseInteger(token->text);
            }
            if (token->kind == TokenKind::Identifier && _identifiersAreZero) {
                return 0;
            }
            return std::nullopt;
        }

        const std::vector<Token>& _tokens;
        bool _identifiersAreZero;
        std::size_t _pos = 0;
        bool _failed = false;
    };

} // namespace

Preprocessor::Preprocessor(SourceManager& sources, PreprocessOptions options)
    : _sources(sources)
    , _options(std::move(options)) {
    _expansions.push_back({}); // ExpansionId 0: not expanded
    _hideSets.emplace_back();   // HideSetId 0: empty
    _hideSetIds.try_emplace(std::vector<std::string_view>{}, 0);
}

std::vector<Token> Preprocessor::run(const std::filesystem::path& path) {
    std::vector<Token> out;
    const auto file = _sources.load(path);
    if (!file) {
        report(Diagnostic::Severity::Error, {}, "cannot read " + path.generic_string());
        return out;
    }
    pushFile(*file, {});
    for (Token token = next(); token.kind != TokenKind::EndOfFile; token = next()) {
        out.push_back(token);
    }
    return out;
}

const Macro* Preprocessor::find(std::string_view name) const {
    const auto it = _active.find(name);
    return it == _active.end() ? nullptr : it->second;
}

SourceLocation Preprocessor::writtenAt(const Token& token) const {
    if (token.expansion == 0) {
        return token.location;
    }
    const Expansion* expansion = &_expansions[token.expansion];
    while (expansion->parent != 0) {
        expansion = &_expansions[expansion->parent];
    }
    return expansion->callSite;
}

std::vector<const Macro*> Preprocessor::macroChain(const Token& token) const {
    std::vector<const Macro*> chain;
    for (ExpansionId id = token.expansion; id != 0; id = _expansions[id].parent) {
        chain.push_back(_expansions[id].macro);
    }
    std::ranges::reverse(chain);
    return chain;
}

std::optional<int64_t> Preprocessor::evaluate(const std::vector<Token>& tokens) {
    const std::vector<Token> expanded = expandList(tokens);
    return ExpressionEvaluator(expanded, false).run();
}

std::optional<int64_t> Preprocessor::evaluate(const Macro& macro) {
    if (macro.functionLike) {
        return std::nullopt;
    }
    return evaluate(macro.body);
}

void Preprocessor::report(Diagnostic::Severity severity, SourceLocation location, std::string message) {
    _diagnostics.push_back({ severity, location, std::move(message) });
}

bool Preprocessor::active() const {
    return _conditionals.empty() || _conditionals.back().active;
}

bool Preprocessor::pushFile(FileId file, SourceLocation includedAt) {
    if (std::cmp_greater_equal(_frames.size(), _options.maxIncludeDepth)) {
        report(Diagnostic::Severity::Error, includedAt, "#include nested too deeply (recursive include?)");
        return false;
    }
    auto [it, fresh] = _lexed.try_emplace(file);
    if (fresh) {
        it->second = lex(_sources.text(file), file, _diagnostics);
    }
    _frames.push_back({ file, &it->second, 0, _conditionals.size() });
    return true;
}

void Preprocessor::popFile() {
    const Frame& frame = _frames.back();
    if (_conditionals.size() > frame.conditionalDepth) {
        report(Diagnostic::Severity::Error, _conditionals.back().location, "#if without #endif");
        _conditionals.resize(frame.conditionalDepth);
    }
    _frames.pop_back();
}

// The next token before macro expansion: expansion output first, then the current file, running
// directives and skipping the branches of #if that are off.
Token Preprocessor::nextRaw() {
    while (true) {
        if (!_pending.empty()) {
            Token token = _pending.front();
            _pending.pop_front();
            return token;
        }
        if (_isolated > 0 || _frames.empty()) {
            return endOfFile();
        }
        Frame& frame = _frames.back();
        const Token& token = (*frame.tokens)[frame.pos];
        if (token.kind == TokenKind::EndOfFile) {
            popFile();
            continue;
        }
        ++frame.pos;
        if (token.atLineStart && token.is("#")) {
            directive(token);
            continue;
        }
        if (active()) {
            return token;
        }
    }
}

Token Preprocessor::next() {
    while (true) {
        Token token = nextRaw();
        if (token.kind != TokenKind::Identifier) {
            return token;
        }
        const Macro* macro = find(token.text);
        if (!macro || hidden(token.hideSet, macro->name) || !expand(token, *macro)) {
            return token;
        }
    }
}

// Fully expand a token list on its own, as a macro argument or an #if line is.
std::vector<Token> Preprocessor::expandList(const std::vector<Token>& tokens) {
    std::deque<Token> saved = std::exchange(_pending, std::deque<Token>(tokens.begin(), tokens.end()));
    ++_isolated;
    std::vector<Token> out;
    for (Token token = next(); token.kind != TokenKind::EndOfFile; token = next()) {
        out.push_back(token);
    }
    --_isolated;
    _pending = std::move(saved);
    return out;
}

bool Preprocessor::expand(const Token& name, const Macro& macro) {
    std::vector<std::vector<Token>> args;
    HideSetId hideSet = 0;
    if (macro.functionLike) {
        Token open = nextRaw();
        if (!open.is("(")) {
            if (open.kind != TokenKind::EndOfFile) {
                _pending.push_front(open); // a function-like macro's name without a call is just a name
            }
            return false;
        }
        std::vector<Token> current;
        int depth = 1;
        Token close;
        while (true) {
            Token token = nextRaw();
            if (token.kind == TokenKind::EndOfFile) {
                report(Diagnostic::Severity::Error, name.location, "unterminated call of macro " + std::string(macro.name));
                break;
            }
            if (token.is("(")) {
                ++depth;
            } else if (token.is(")") && --depth == 0) {
                close = token;
                break;
            }
            const bool separates = token.is(",") && depth == 1 && !(macro.variadic && args.size() == macro.params.size());
            if (separates) {
                args.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(token);
            }
        }
        args.push_back(std::move(current));
        if (macro.params.empty() && !macro.variadic && args.size() == 1 && args[0].empty()) {
            args.clear(); // f() passes no arguments
        }
        const std::size_t expected = macro.params.size() + (macro.variadic ? 1 : 0);
        if (args.size() != expected && !(macro.variadic && args.size() == macro.params.size())) {
            report(Diagnostic::Severity::Error, name.location,
                "macro " + std::string(macro.name) + " takes " + std::to_string(macro.params.size()) + " arguments, "
                    + std::to_string(args.size()) + " given");
        }
        args.resize(expected);
        hideSet = withName(intersect(name.hideSet, close.hideSet), macro.name);
    } else {
        hideSet = withName(name.hideSet, macro.name);
    }

    const auto expansion = static_cast<ExpansionId>(_expansions.size());
    _expansions.push_back({ &macro, name.location, name.expansion });
    std::vector<Token> out = substitute(macro, args, expansion);
    for (Token& token : out) {
        token.hideSet = unite(token.hideSet, hideSet);
        token.atLineStart = false;
    }
    if (!out.empty()) {
        out.front().spaceBefore = name.spaceBefore;
    }
    _pending.insert(_pending.begin(), out.begin(), out.end());
    return true;
}

// The macro body with its parameters replaced: by the argument as written next to `#` or `##`,
// otherwise by the fully expanded argument. Body tokens are marked as coming from `expansion`;
// argument tokens keep their own origin, which is where the user wrote them.
std::vector<Token> Preprocessor::substitute(const Macro& macro, const std::vector<std::vector<Token>>& args, ExpansionId expansion) {
    const auto paramIndex = [&macro](const Token& token) -> std::optional<std::size_t> {
        if (token.kind != TokenKind::Identifier) {
            return std::nullopt;
        }
        if (macro.variadic && token.text == VA_ARGS) {
            return macro.params.size();
        }
        const auto it = std::ranges::find(macro.params, token.text);
        return it == macro.params.end() ? std::nullopt : std::optional(static_cast<std::size_t>(it - macro.params.begin()));
    };
    std::map<std::size_t, std::vector<Token>> expandedArgs;
    const auto operand = [&](std::size_t i, bool raw) {
        const Token& token = macro.body[i];
        if (const auto param = paramIndex(token); param && *param < args.size()) {
            if (raw) {
                return args[*param];
            }
            auto [it, fresh] = expandedArgs.try_emplace(*param);
            if (fresh) {
                it->second = expandList(args[*param]);
            }
            return it->second;
        }
        Token copy = token;
        copy.expansion = expansion;
        return std::vector<Token>{ copy };
    };

    std::vector<Token> out;
    const std::vector<Token>& body = macro.body;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const Token& token = body[i];
        if (macro.functionLike && token.is("#") && i + 1 < body.size() && paramIndex(body[i + 1])) {
            Token text = stringize(operand(i + 1, true));
            text.expansion = expansion;
            text.location = token.location;
            out.push_back(text);
            ++i;
            continue;
        }
        if (token.is("##") && i + 1 < body.size()) {
            std::vector<Token> right = operand(i + 1, true);
            ++i;
            if (out.empty() || right.empty()) {
                out.insert(out.end(), right.begin(), right.end());
                continue;
            }
            out.back() = paste(out.back(), right.front());
            out.back().expansion = expansion;
            out.insert(out.end(), right.begin() + 1, right.end());
            continue;
        }
        const bool beforePaste = i + 1 < body.size() && body[i + 1].is("##");
        std::vector<Token> tokens = operand(i, beforePaste);
        if (!tokens.empty()) {
            tokens.front().spaceBefore = token.spaceBefore;
        }
        out.insert(out.end(), tokens.begin(), tokens.end());
    }
    return out;
}

Token Preprocessor::paste(const Token& left, const Token& right) {
    const std::string_view text = keep(std::string(left.text) + std::string(right.text));
    std::vector<Diagnostic> ignored;
    const std::vector<Token> relexed = lex(text, left.location.file, ignored);
    Token pasted = left;
    pasted.text = text;
    if (relexed.size() == 2 && ignored.empty()) {
        pasted.kind = relexed.front().kind;
    } else {
        report(Diagnostic::Severity::Warning, left.location,
            "pasting \"" + std::string(left.text) + "\" and \"" + std::string(right.text) + "\" does not give one token");
    }
    return pasted;
}

Token Preprocessor::stringize(const std::vector<Token>& arg) {
    std::string text = "\"";
    for (std::size_t i = 0; i < arg.size(); ++i) {
        if (i > 0 && arg[i].spaceBefore) {
            text += ' ';
        }
        for (char c : arg[i].text) {
            const bool quoted = arg[i].kind == TokenKind::String || arg[i].kind == TokenKind::Char;
            if (quoted && (c == '"' || c == '\\')) {
                text += '\\';
            }
            text += c;
        }
    }
    text += '"';
    Token token;
    token.kind = TokenKind::String;
    token.text = keep(std::move(text));
    return token;
}

std::string_view Preprocessor::keep(std::string text) {
    _synthesized.push_back(std::make_unique<std::string>(std::move(text)));
    return *_synthesized.back();
}

// The rest of the current directive's line.
std::vector<Token> Preprocessor::directiveLine() {
    std::vector<Token> line;
    Frame& frame = _frames.back();
    while (true) {
        const Token& token = (*frame.tokens)[frame.pos];
        if (token.kind == TokenKind::EndOfFile || token.atLineStart) {
            return line;
        }
        line.push_back(token);
        ++frame.pos;
    }
}

void Preprocessor::directive(const Token& hash) {
    std::vector<Token> line = directiveLine();
    if (line.empty()) {
        return; // the null directive
    }
    const Token name = line.front();
    line.erase(line.begin());
    const std::string_view kind = name.text;

    if (kind == "ifdef" || kind == "ifndef" || kind == "if") {
        const bool parentActive = active();
        bool value = false;
        if (parentActive && kind == "if") {
            value = evaluateCondition(line);
        } else if (parentActive) {
            const bool defined = !line.empty() && find(line.front().text) != nullptr;
            value = kind == "ifdef" ? defined : !defined;
        }
        _conditionals.push_back({ value, value, false, parentActive, hash.location });
        return;
    }
    if (kind == "elif" || kind == "else" || kind == "endif") {
        if (_conditionals.size() <= _frames.back().conditionalDepth) {
            report(Diagnostic::Severity::Error, hash.location, "#" + std::string(kind) + " without #if");
            return;
        }
        Conditional& conditional = _conditionals.back();
        if (kind == "endif") {
            _conditionals.pop_back();
            return;
        }
        if (conditional.sawElse) {
            report(Diagnostic::Severity::Error, hash.location, "#" + std::string(kind) + " after #else");
        }
        if (kind == "else") {
            conditional.sawElse = true;
            conditional.active = conditional.parentActive && !conditional.taken;
        } else {
            conditional.active = conditional.parentActive && !conditional.taken && evaluateCondition(line);
        }
        conditional.taken = conditional.taken || conditional.active;
        return;
    }
    if (!active()) {
        return;
    }
    if (kind == "include") {
        include(line, name);
    } else if (kind == "define") {
        define(line, name);
    } else if (kind == "undef") {
        if (!line.empty()) {
            _active.erase(line.front().text);
        }
    } else if (kind == "error") {
        report(Diagnostic::Severity::Error, hash.location, "#error");
    } else if (kind != "pragma" && kind != "line" && kind != "warning") {
        report(Diagnostic::Severity::Warning, hash.location, "unknown directive #" + std::string(kind));
    }
}

void Preprocessor::include(const std::vector<Token>& written, const Token& directive) {
    // `#include NAME`: the file name comes from expanding the line (RP zilocker.ssl, EXTRA_HEADER).
    const bool computed = !written.empty() && written.front().kind == TokenKind::Identifier;
    const std::vector<Token> line = computed ? expandList(written) : written;
    std::string spelled;
    bool quoted = false;
    if (!line.empty() && line.front().kind == TokenKind::String && line.front().text.size() >= 2) {
        spelled = std::string(line.front().text.substr(1, line.front().text.size() - 2));
        quoted = true;
    } else if (!line.empty() && line.front().is("<")) {
        for (std::size_t i = 1; i < line.size() && !line[i].is(">"); ++i) {
            spelled += line[i].text;
        }
    }
    if (spelled.empty()) {
        report(Diagnostic::Severity::Error, directive.location, "#include expects \"file\" or <file>");
        return;
    }
    std::ranges::replace(spelled, '\\', '/'); // sources written for Windows: "..\headers\define.h"

    std::vector<std::filesystem::path> candidates;
    if (quoted) {
        candidates.push_back(_sources.path(_frames.back().file).parent_path() / spelled);
    }
    for (const auto& dir : _options.includeDirs) {
        candidates.push_back(dir / spelled);
    }
    for (const auto& candidate : candidates) {
        if (const auto file = _sources.load(candidate)) {
            pushFile(*file, directive.location);
            return;
        }
    }
    report(Diagnostic::Severity::Error, directive.location, "cannot open include file " + spelled);
}

void Preprocessor::define(const std::vector<Token>& line, const Token& directive) {
    if (line.empty() || line.front().kind != TokenKind::Identifier) {
        report(Diagnostic::Severity::Error, directive.location, "#define expects a macro name");
        return;
    }
    Macro macro;
    macro.name = line.front().text;
    macro.location = line.front().location;
    std::size_t i = 1;
    if (i < line.size() && line[i].is("(") && !line[i].spaceBefore) {
        macro.functionLike = true;
        for (++i; i < line.size() && !line[i].is(")"); ++i) {
            if (line[i].is("...")) {
                macro.variadic = true;
            } else if (line[i].kind == TokenKind::Identifier) {
                macro.params.push_back(line[i].text);
            } else if (!line[i].is(",")) {
                report(Diagnostic::Severity::Error, line[i].location, "bad parameter list for macro " + std::string(macro.name));
            }
        }
        ++i; // the ')'
    }
    if (i < line.size()) {
        macro.body.assign(line.begin() + static_cast<std::ptrdiff_t>(i), line.end());
        macro.body.front().spaceBefore = false;
    }
    _definitions.push_back(std::move(macro));
    _active[_definitions.back().name] = &_definitions.back();
}

bool Preprocessor::evaluateCondition(const std::vector<Token>& line) {
    // `defined X` and `defined(X)` are answered before expansion, so X's own definition is not used.
    std::vector<Token> resolved;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (!line[i].isIdentifier("defined")) {
            resolved.push_back(line[i]);
            continue;
        }
        const bool parens = i + 1 < line.size() && line[i + 1].is("(");
        const std::size_t nameAt = i + (parens ? 2 : 1);
        if (nameAt >= line.size()) {
            report(Diagnostic::Severity::Error, line[i].location, "defined without a macro name");
            return false;
        }
        resolved.push_back(synthesizedNumber(find(line[nameAt].text) != nullptr ? 1 : 0, line[i]));
        i = nameAt + (parens ? 1 : 0);
    }
    const auto value = ExpressionEvaluator(expandList(resolved), true).run();
    if (!value) {
        report(Diagnostic::Severity::Error, line.empty() ? SourceLocation{} : line.front().location, "malformed #if expression");
        return false;
    }
    return *value != 0;
}

bool Preprocessor::hidden(HideSetId set, std::string_view name) const {
    const auto& names = _hideSets[set];
    return std::ranges::binary_search(names, name);
}

HideSetId Preprocessor::withName(HideSetId set, std::string_view name) {
    std::vector<std::string_view> names = _hideSets[set];
    names.push_back(name);
    return intern(std::move(names));
}

HideSetId Preprocessor::unite(HideSetId a, HideSetId b) {
    if (a == b || b == 0) {
        return a;
    }
    if (a == 0) {
        return b;
    }
    std::vector<std::string_view> names = _hideSets[a];
    names.insert(names.end(), _hideSets[b].begin(), _hideSets[b].end());
    return intern(std::move(names));
}

HideSetId Preprocessor::intersect(HideSetId a, HideSetId b) {
    if (a == b) {
        return a;
    }
    std::vector<std::string_view> names;
    std::ranges::set_intersection(_hideSets[a], _hideSets[b], std::back_inserter(names));
    return intern(std::move(names));
}

HideSetId Preprocessor::intern(std::vector<std::string_view> names) {
    std::ranges::sort(names);
    const auto [first, last] = std::ranges::unique(names);
    names.erase(first, last);
    if (const auto it = _hideSetIds.find(names); it != _hideSetIds.end()) {
        return it->second;
    }
    const auto id = static_cast<HideSetId>(_hideSets.size());
    _hideSets.push_back(names);
    _hideSetIds.try_emplace(std::move(names), id);
    return id;
}

} // namespace geck::ssl
