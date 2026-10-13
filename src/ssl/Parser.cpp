#include "ssl/Parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <string_view>

namespace geck::ssl {

namespace {

    bool equalsIgnoreCase(std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    // Words sslc reserves (lex.c); none of them can name a variable or procedure.
    constexpr std::array<std::string_view, 49> KEYWORDS{ "procedure", "variable", "import", "export", "in", "when", "pure",
        "inline", "critical", "begin", "end", "exit", "detach", "fork", "spawn", "callstart", "exec", "noop", "wait", "call",
        "cancel", "cancelall", "if", "then", "else", "switch", "case", "default", "and", "or", "bwand", "bwor", "bwnot",
        "bwxor", "not", "floor", "return", "do", "while", "for", "foreach", "break", "continue", "andalso", "orelse", "div",
        "include", "startcritical", "endcritical" };

    bool isReserved(const Token& token) {
        return token.kind == TokenKind::Identifier
            && std::ranges::any_of(KEYWORDS, [&token](std::string_view keyword) { return equalsIgnoreCase(token.text, keyword); });
    }

    // sslc lex.c escapes; like sslc, \v gives a tab.
    std::string unescape(std::string_view quoted) {
        const std::string_view text = quoted.size() >= 2 ? quoted.substr(1, quoted.size() - 2) : std::string_view{};
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\\' || i + 1 == text.size()) {
                out += text[i];
                continue;
            }
            switch (const char c = text[++i]; c) {
                case 'a':
                    out += '\a';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                case 'v':
                    out += '\t';
                    break;
                default:
                    out += c;
                    break;
            }
        }
        return out;
    }

    template <typename Node>
    ExprPtr makeExpr(Node node, std::size_t begin, std::size_t end) {
        auto expr = std::make_unique<Expr>();
        expr->node = std::move(node);
        expr->range = { begin, end };
        return expr;
    }

    template <typename Node>
    StmtPtr makeStmt(Node node, std::size_t begin, std::size_t end) {
        auto stmt = std::make_unique<Stmt>();
        stmt->node = std::move(node);
        stmt->range = { begin, end };
        return stmt;
    }

    class Parser {
    public:
        Parser(const std::vector<Token>& tokens, const Preprocessor& preprocessor, std::vector<Diagnostic>& diagnostics)
            : _tokens(tokens)
            , _pp(preprocessor)
            , _diagnostics(diagnostics) {}

        Program run() {
            Program program;
            while (!atEnd()) {
                const std::size_t before = _pos;
                topLevel(program);
                if (_panic) {
                    skipToDeclaration();
                    _panic = false;
                }
                if (_pos == before) {
                    ++_pos; // always make progress
                }
            }
            return program;
        }

    private:
        // --- token access -------------------------------------------------------------------

        const Token& peek(std::size_t ahead = 0) const {
            static const Token END{};
            return _pos + ahead < _tokens.size() ? _tokens[_pos + ahead] : END;
        }

        bool atEnd() const { return _pos >= _tokens.size(); }

        static bool isKeyword(const Token& token, std::string_view keyword) {
            return token.kind == TokenKind::Identifier && equalsIgnoreCase(token.text, keyword);
        }

        bool acceptKeyword(std::string_view keyword) {
            if (isKeyword(peek(), keyword)) {
                ++_pos;
                return true;
            }
            return false;
        }

        bool acceptPunct(std::string_view punct) {
            if (peek().is(punct)) {
                ++_pos;
                return true;
            }
            return false;
        }

        // Report a syntax error at the current token and stop parsing the current construct.
        void error(std::string message) {
            if (!_panic) {
                const SourceLocation where = atEnd() ? lastLocation() : _pp.writtenAt(peek());
                const std::string_view found = atEnd() ? std::string_view("end of file") : peek().text;
                _diagnostics.emplace_back(Diagnostic::Severity::Error, where, std::format("{}, found '{}'", message, found));
            }
            _panic = true;
        }

        SourceLocation lastLocation() const {
            return _tokens.empty() ? SourceLocation{} : _pp.writtenAt(_tokens.back());
        }

        bool expectKeyword(std::string_view keyword) {
            if (acceptKeyword(keyword)) {
                return true;
            }
            error(std::format("expected '{}'", keyword));
            return false;
        }

        bool expectPunct(std::string_view punct) {
            if (acceptPunct(punct)) {
                return true;
            }
            error(std::format("expected '{}'", punct));
            return false;
        }

        std::optional<std::string> expectName(std::string_view what) {
            if (peek().kind == TokenKind::Identifier && !isReserved(peek())) {
                return std::string(_tokens[_pos++].text);
            }
            error(std::format("expected {}", what));
            return std::nullopt;
        }

        // sslc lexes a lone '=' as the assignment ':=' (lex.c, EXPECT_ASSIGN); '==' is the comparison.
        bool acceptAssign() { return acceptPunct(":=") || acceptPunct("="); }

        // A missing ';' is reported but does not stop the statement: sslc reads on the same way.
        void expectSemicolon() {
            if (!acceptPunct(";") && !_panic) {
                const SourceLocation where = _pos > 0 ? _pp.writtenAt(_tokens[_pos - 1]) : SourceLocation{};
                _diagnostics.emplace_back(Diagnostic::Severity::Error, where, "expected ';'");
            }
        }

        // --- recovery -----------------------------------------------------------------------

        // Skip the rest of a broken statement: past the next ';' at this nesting, or up to the `end`
        // that closes the enclosing block.
        void skipStatement() {
            int depth = 0;
            while (!atEnd()) {
                if (isKeyword(peek(), "begin")) {
                    ++depth;
                } else if (isKeyword(peek(), "end")) {
                    if (depth == 0) {
                        return;
                    }
                    --depth;
                } else if (peek().is(";") && depth == 0) {
                    ++_pos;
                    return;
                }
                ++_pos;
            }
        }

        void skipToDeclaration() {
            while (!atEnd()) {
                const Token& token = peek();
                const bool declaration = isKeyword(token, "procedure") || isKeyword(token, "variable") || isKeyword(token, "import")
                    || isKeyword(token, "export") || isKeyword(token, "critical") || isKeyword(token, "pure") || isKeyword(token, "inline");
                if (declaration && token.atLineStart) {
                    return;
                }
                ++_pos;
            }
        }

        // --- declarations -------------------------------------------------------------------

        void topLevel(Program& program) {
            if (acceptPunct(";")) {
                return;
            }
            if (acceptKeyword("variable")) {
                program.variables.push_back({ Linkage::Local, variables(true) });
                return;
            }
            if (isKeyword(peek(), "import") || isKeyword(peek(), "export")) {
                external(program);
                return;
            }
            if (isKeyword(peek(), "procedure") || isKeyword(peek(), "critical") || isKeyword(peek(), "pure") || isKeyword(peek(), "inline")) {
                if (auto procedure = procedureDeclaration(Linkage::Local); procedure.has_value()) {
                    program.procedures.push_back(std::move(*procedure));
                }
                return;
            }
            error("expected a procedure or variable declaration");
        }

        // `import`/`export` followed by `variable ...` or `procedure ...`, each alone or in a begin/end block.
        void external(Program& program) {
            const Linkage linkage = isKeyword(peek(), "import") ? Linkage::Import : Linkage::Export;
            ++_pos; // the import or export keyword
            if (acceptKeyword("variable")) {
                program.variables.push_back({ linkage, variables(true) });
                return;
            }
            if (!expectKeyword("procedure")) {
                return;
            }
            const bool block = acceptKeyword("begin");
            do {
                if (auto procedure = procedureSignature(linkage); procedure.has_value()) {
                    expectSemicolon();
                    program.procedures.push_back(std::move(*procedure));
                }
            } while (block && !_panic && !atEnd() && !acceptKeyword("end"));
        }

        std::optional<Procedure> procedureDeclaration(Linkage linkage) {
            const std::size_t begin = _pos;
            bool critical = false;
            bool pure = false;
            bool inlined = false;
            while (true) {
                if (acceptKeyword("critical")) {
                    critical = true;
                } else if (acceptKeyword("pure")) {
                    pure = true;
                } else if (acceptKeyword("inline")) {
                    inlined = true;
                } else {
                    break;
                }
            }
            if (!expectKeyword("procedure")) {
                return std::nullopt;
            }
            auto procedure = procedureSignature(linkage);
            if (!procedure.has_value()) {
                return std::nullopt;
            }
            procedure->critical = critical;
            procedure->pure = pure;
            procedure->inlined = inlined;
            if (acceptPunct(";")) {
                procedure->range = { begin, _pos };
                return procedure; // a forward declaration
            }
            if (acceptKeyword("in")) {
                procedure->timedDelay = expression();
            } else if (acceptKeyword("when")) {
                procedure->condition = expression();
            }
            if (!expectKeyword("begin")) {
                return std::nullopt;
            }
            procedure->body = blockBody();
            procedure->range = { begin, _pos };
            return procedure;
        }

        // `name` or `name(variable a, variable b := 1)`, after `procedure`.
        std::optional<Procedure> procedureSignature(Linkage linkage) {
            const std::size_t begin = _pos;
            auto name = expectName("a procedure name");
            if (!name.has_value()) {
                return std::nullopt;
            }
            Procedure procedure;
            procedure.name = std::move(*name);
            procedure.linkage = linkage;
            // sslc reads parameters until ')', so a trailing comma before it is accepted.
            if (acceptPunct("(") && !acceptPunct(")")) {
                do {
                    if (acceptPunct(")")) {
                        procedure.range = { begin, _pos };
                        return procedure;
                    }
                    if (!expectKeyword("variable")) {
                        return std::nullopt;
                    }
                    auto param = expectName("a parameter name");
                    if (!param.has_value()) {
                        return std::nullopt;
                    }
                    Parameter parameter{ std::move(*param), nullptr };
                    if (acceptAssign()) {
                        parameter.defaultValue = expression();
                    }
                    procedure.parameters.push_back(std::move(parameter));
                } while (acceptPunct(","));
                expectPunct(")");
            }
            procedure.range = { begin, _pos };
            return procedure;
        }

        // After `variable`: one declaration list ended by ';', or a begin/end block of them.
        // A global list may omit its ';' (sslc's backward compatibility).
        std::vector<Variable> variables(bool global) {
            std::vector<Variable> declared;
            if (acceptKeyword("begin")) {
                while (!_panic && !atEnd() && !acceptKeyword("end")) {
                    variableList(declared);
                    expectSemicolon();
                }
                return declared;
            }
            variableList(declared);
            if (global) {
                acceptPunct(";");
            } else {
                expectSemicolon();
            }
            return declared;
        }

        // `a, b := 1, c[10]`
        void variableList(std::vector<Variable>& declared) {
            do {
                const std::size_t begin = _pos;
                auto name = expectName("a variable name");
                if (!name.has_value()) {
                    return;
                }
                Variable variable;
                variable.name = std::move(*name);
                if (acceptPunct("[")) {
                    variable.arraySize = expression();
                    if (acceptPunct(",")) {
                        expression(); // array flags
                    }
                    expectPunct("]");
                } else if (acceptAssign()) {
                    variable.initializer = expression();
                }
                variable.range = { begin, _pos };
                declared.push_back(std::move(variable));
            } while (!_panic && acceptPunct(","));
        }

        // --- statements ---------------------------------------------------------------------

        // The statements after `begin`, through the matching `end`.
        Block blockBody() {
            Block block;
            while (!atEnd() && !isKeyword(peek(), "end")) {
                const std::size_t before = _pos;
                StmtPtr stmt = statement(true);
                if (_panic) {
                    if (_pos == 0 || !_tokens[_pos - 1].is(";")) {
                        skipStatement(); // unless the broken statement already reached its ';'
                    }
                    _panic = false;
                } else if (stmt) {
                    block.statements.push_back(std::move(stmt));
                }
                if (_pos == before) {
                    ++_pos;
                }
            }
            expectKeyword("end");
            return block;
        }

        StmtPtr statement(bool requireSemicolon) {
            const std::size_t begin = _pos;
            const Token& token = peek();
            if (token.is(";")) {
                ++_pos;
                return makeStmt(Empty{}, begin, _pos);
            }
            if (acceptKeyword("begin")) {
                Block block = blockBody();
                return makeStmt(std::move(block), begin, _pos);
            }
            if (acceptKeyword("if")) {
                return ifStatement(begin);
            }
            if (acceptKeyword("while")) {
                return whileStatement(begin);
            }
            if (acceptKeyword("for")) {
                return forStatement(begin);
            }
            if (acceptKeyword("foreach")) {
                return forEachStatement(begin);
            }
            if (acceptKeyword("switch")) {
                return switchStatement(begin);
            }
            if (acceptKeyword("variable")) {
                return makeStmt(LocalVariables{ variables(false) }, begin, _pos);
            }
            StmtPtr simple = simpleStatement();
            if (simple && requireSemicolon) {
                expectSemicolon();
            }
            if (simple) {
                simple->range = { begin, _pos };
            }
            return simple;
        }

        // A statement that ends with ';': return, call, the keyword statements, an assignment or an
        // expression. The ';' itself is left to the caller (a for loop's step has none).
        StmtPtr simpleStatement() {
            const std::size_t begin = _pos;
            if (acceptKeyword("return")) {
                Return ret;
                if (!peek().is(";")) {
                    ret.value = expression();
                }
                return makeStmt(std::move(ret), begin, _pos);
            }
            if (acceptKeyword("call")) {
                return callStatement(begin);
            }
            if (StmtPtr keyword = keywordStatement(begin)) {
                return keyword;
            }
            if (isAssignmentStart()) {
                return assignment(begin);
            }
            ExprPtr expr = expression();
            if (!expr) {
                return nullptr;
            }
            return makeStmt(ExpressionStatement{ std::move(expr) }, begin, _pos);
        }

        StmtPtr keywordStatement(std::size_t begin) {
            static constexpr std::array<std::string_view, 8> BARE{ "break", "continue", "exit", "detach", "noop", "cancelall",
                "startcritical", "endcritical" };
            static constexpr std::array<std::string_view, 6> WITH_OPERAND{ "cancel", "wait", "fork", "spawn", "exec", "callstart" };
            for (std::string_view keyword : BARE) {
                if (acceptKeyword(keyword)) {
                    return makeStmt(KeywordStatement{ std::string(keyword), nullptr }, begin, _pos);
                }
            }
            for (std::string_view keyword : WITH_OPERAND) {
                if (acceptKeyword(keyword)) {
                    ExprPtr operand = expression();
                    return makeStmt(KeywordStatement{ std::string(keyword), std::move(operand) }, begin, _pos);
                }
            }
            return nullptr;
        }

        // sslc: a name followed by an assignment operator, ++/--, '[' or '.' starts an assignment.
        bool isAssignmentStart() const {
            if (peek().kind != TokenKind::Identifier || isReserved(peek())) {
                return false;
            }
            const Token& next = peek(1);
            return next.is(":=") || next.is("=") || next.is("+=") || next.is("-=") || next.is("*=") || next.is("/=") || next.is("++")
                || next.is("--") || next.is("[") || next.is(".");
        }

        StmtPtr assignment(std::size_t begin) {
            ExprPtr target = makeExpr(Name{ std::string(_tokens[_pos].text) }, _pos, _pos + 1);
            ++_pos;
            target = postfix(std::move(target), begin);
            if (!target) {
                return nullptr;
            }
            for (std::string_view op : { "++", "--" }) {
                if (acceptPunct(op)) {
                    return makeStmt(Assignment{ std::move(target), std::string(op), nullptr }, begin, _pos);
                }
            }
            if (acceptAssign()) {
                ExprPtr value = expression();
                return makeStmt(Assignment{ std::move(target), ":=", std::move(value) }, begin, _pos);
            }
            for (std::string_view op : { "+=", "-=", "*=", "/=" }) {
                if (acceptPunct(op)) {
                    ExprPtr value = expression();
                    return makeStmt(Assignment{ std::move(target), std::string(op), std::move(value) }, begin, _pos);
                }
            }
            error("expected an assignment operator");
            return nullptr;
        }

        StmtPtr callStatement(std::size_t begin) {
            CallStatement call;
            const std::size_t targetAt = _pos;
            if (peek().kind == TokenKind::String) {
                ++_pos;
                call.target = makeExpr(StringLiteral{ unescape(_tokens[targetAt].text) }, targetAt, _pos);
            } else if (auto name = expectName("a procedure to call"); name.has_value()) {
                call.target = makeExpr(Name{ std::move(*name) }, targetAt, _pos);
            } else {
                return nullptr;
            }
            if (acceptPunct("(")) {
                call.args = arguments();
            }
            if (acceptKeyword("in")) {
                call.schedule = "in";
                call.scheduleValue = expression();
            } else if (acceptKeyword("when")) {
                call.schedule = "when";
                call.scheduleValue = expression();
            }
            return makeStmt(std::move(call), begin, _pos);
        }

        StmtPtr ifStatement(std::size_t begin) {
            If node;
            node.condition = expression();
            if (!expectKeyword("then")) {
                return nullptr;
            }
            node.then = statement(true);
            if (!_panic && acceptKeyword("else")) {
                node.otherwise = statement(true);
            }
            return makeStmt(std::move(node), begin, _pos);
        }

        StmtPtr whileStatement(std::size_t begin) {
            While node;
            node.condition = expression();
            if (!expectKeyword("do")) {
                return nullptr;
            }
            node.body = statement(true);
            return makeStmt(std::move(node), begin, _pos);
        }

        // sfall `for (init; condition; step) body`; the parentheses are optional, and without them the
        // step ends with ';'.
        StmtPtr forStatement(std::size_t begin) {
            For node;
            const bool parenthesised = acceptPunct("(");
            node.init = statement(true);
            node.condition = expression();
            expectPunct(";");
            node.step = statement(false);
            if (parenthesised) {
                expectPunct(")");
            } else {
                expectPunct(";");
            }
            node.body = statement(true);
            return makeStmt(std::move(node), begin, _pos);
        }

        // sfall `foreach (variable key: value in collection while condition) body`.
        StmtPtr forEachStatement(std::size_t begin) {
            ForEach node;
            const bool parenthesised = acceptPunct("(");
            node.declares = acceptKeyword("variable");
            auto first = expectName("a loop variable");
            if (!first.has_value()) {
                return nullptr;
            }
            if (acceptPunct(":")) {
                node.key = std::move(*first);
                auto value = expectName("a loop variable");
                if (!value.has_value()) {
                    return nullptr;
                }
                node.value = std::move(*value);
            } else {
                node.value = std::move(*first);
            }
            if (!expectKeyword("in")) {
                return nullptr;
            }
            node.collection = expression();
            if (acceptKeyword("while")) {
                node.whileCondition = expression();
            }
            if (parenthesised) {
                expectPunct(")");
            }
            node.body = statement(true);
            return makeStmt(std::move(node), begin, _pos);
        }

        // sfall `switch subject begin case v: ... default: ... end`.
        StmtPtr switchStatement(std::size_t begin) {
            Switch node;
            node.subject = expression();
            if (!expectKeyword("begin")) {
                return nullptr;
            }
            while (!_panic && !atEnd() && !acceptKeyword("end")) {
                SwitchCase branch;
                if (acceptKeyword("case")) {
                    branch.value = expression();
                } else if (!acceptKeyword("default")) {
                    error("expected 'case', 'default' or 'end'");
                    return nullptr;
                }
                expectPunct(":");
                while (!_panic && !atEnd() && !isKeyword(peek(), "case") && !isKeyword(peek(), "default") && !isKeyword(peek(), "end")) {
                    if (StmtPtr stmt = statement(true)) {
                        branch.body.push_back(std::move(stmt));
                    }
                }
                node.cases.push_back(std::move(branch));
            }
            return makeStmt(std::move(node), begin, _pos);
        }

        // --- expressions --------------------------------------------------------------------

        ExprPtr expression() {
            const std::size_t begin = _pos;
            ExprPtr value = logical();
            if (value && acceptKeyword("if")) {
                ExprPtr condition = expression();
                if (!expectKeyword("else")) {
                    return nullptr;
                }
                ExprPtr otherwise = expression();
                return makeExpr(Conditional{ std::move(value), std::move(condition), std::move(otherwise) }, begin, _pos);
            }
            return value;
        }

        // The binary operator at the current token if it is one of `operators`, lowercased.
        std::optional<std::string> binaryOperator(std::initializer_list<std::string_view> operators) const {
            const Token& token = peek();
            for (std::string_view op : operators) {
                const bool word = std::isalpha(static_cast<unsigned char>(op.front())) != 0;
                if (word ? isKeyword(token, op) : token.is(op)) {
                    return std::string(op);
                }
            }
            return std::nullopt;
        }

        // Left-associative operators at one precedence level over operands from `operand`.
        template <typename Operand>
        ExprPtr leftAssociative(std::initializer_list<std::string_view> operators, Operand operand) {
            const std::size_t begin = _pos;
            ExprPtr left = (this->*operand)();
            while (left) {
                auto op = binaryOperator(operators);
                if (!op.has_value()) {
                    break;
                }
                ++_pos;
                ExprPtr right = (this->*operand)();
                if (!right) {
                    return nullptr;
                }
                left = makeExpr(Binary{ std::move(*op), std::move(left), std::move(right) }, begin, _pos);
            }
            return left;
        }

        ExprPtr logical() { return leftAssociative({ "and", "or", "andalso", "orelse" }, &Parser::comparison); }

        // One comparison, not a chain: `a < b < c` is not SSL.
        ExprPtr comparison() {
            const std::size_t begin = _pos;
            ExprPtr left = additive();
            auto op = left != nullptr ? binaryOperator({ "==", "!=", "<=", ">=", "<", ">" }) : std::nullopt;
            if (!op.has_value()) {
                return left;
            }
            ++_pos;
            ExprPtr right = additive();
            if (!right) {
                return nullptr;
            }
            return makeExpr(Binary{ std::move(*op), std::move(left), std::move(right) }, begin, _pos);
        }

        ExprPtr additive() { return leftAssociative({ "+", "-", "bwand", "bwor", "bwxor" }, &Parser::term); }

        ExprPtr term() { return leftAssociative({ "*", "/", "%", "div", "^" }, &Parser::factor); }

        ExprPtr factor() {
            const std::size_t begin = _pos;
            for (std::string_view op : { "-", "not", "bwnot", "floor" }) {
                const bool word = op != "-";
                if (word ? acceptKeyword(op) : acceptPunct(op)) {
                    ExprPtr operand = factor();
                    if (!operand) {
                        return nullptr;
                    }
                    return makeExpr(Unary{ std::string(op), std::move(operand) }, begin, _pos);
                }
            }
            if (acceptPunct("@")) {
                auto name = expectName("a procedure name after '@'");
                return name.has_value() ? makeExpr(ProcedureRef{ std::move(*name) }, begin, _pos) : nullptr;
            }
            ExprPtr value = primary();
            return value ? postfix(std::move(value), begin) : nullptr;
        }

        ExprPtr primary() {
            const std::size_t begin = _pos;
            const Token& token = peek();
            if (token.kind == TokenKind::Number) {
                ++_pos;
                return number(token, begin);
            }
            if (token.kind == TokenKind::String) {
                ++_pos;
                return makeExpr(StringLiteral{ unescape(token.text) }, begin, _pos);
            }
            if (token.kind == TokenKind::Identifier && !isReserved(token)) {
                std::string name(token.text);
                ++_pos;
                if (acceptPunct("(")) {
                    std::vector<ExprPtr> args = arguments();
                    return _panic ? nullptr : makeExpr(Call{ std::move(name), std::move(args) }, begin, _pos);
                }
                return makeExpr(Name{ std::move(name) }, begin, _pos);
            }
            if (acceptPunct("(")) {
                ExprPtr inner = expression();
                return inner && expectPunct(")") ? std::move(inner) : nullptr;
            }
            if (acceptPunct("[")) {
                ArrayLiteral array;
                if (!acceptPunct("]")) {
                    array.elements = list("]");
                }
                return _panic ? nullptr : makeExpr(std::move(array), begin, _pos);
            }
            if (acceptPunct("{")) {
                return mapLiteral(begin);
            }
            error("expected an expression");
            return nullptr;
        }

        ExprPtr number(const Token& token, std::size_t begin) {
            if (auto value = integerValue(token.text); value.has_value()) {
                return makeExpr(IntLiteral{ *value }, begin, _pos);
            }
            double value = 0;
            const auto [end, ec] = std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
            if (ec != std::errc() || end != token.text.data() + token.text.size()) {
                --_pos;
                error("malformed number");
                return nullptr;
            }
            return makeExpr(FloatLiteral{ value }, begin, _pos);
        }

        // `target[index]` and `target.member`, repeated.
        ExprPtr postfix(ExprPtr target, std::size_t begin) {
            while (target) {
                if (acceptPunct("[")) {
                    ExprPtr index = expression();
                    if (!index || !expectPunct("]")) {
                        return nullptr;
                    }
                    target = makeExpr(Index{ std::move(target), std::move(index) }, begin, _pos);
                } else if (peek().is(".") && peek(1).kind == TokenKind::Identifier) {
                    ++_pos;
                    std::string member(_tokens[_pos++].text);
                    target = makeExpr(Member{ std::move(target), std::move(member) }, begin, _pos);
                } else {
                    break;
                }
            }
            return target;
        }

        // `a, b, c` up to and including `close`.
        std::vector<ExprPtr> list(std::string_view close) {
            std::vector<ExprPtr> items;
            do {
                ExprPtr item = expression();
                if (!item) {
                    return items;
                }
                items.push_back(std::move(item));
            } while (acceptPunct(","));
            expectPunct(close);
            return items;
        }

        // A call's arguments after its '('.
        std::vector<ExprPtr> arguments() {
            if (acceptPunct(")")) {
                return {};
            }
            return list(")");
        }

        ExprPtr mapLiteral(std::size_t begin) {
            MapLiteral map;
            if (acceptPunct("}")) {
                return makeExpr(std::move(map), begin, _pos);
            }
            do {
                ExprPtr key = expression();
                if (!key || !expectPunct(":")) {
                    return nullptr;
                }
                ExprPtr value = expression();
                if (!value) {
                    return nullptr;
                }
                map.entries.emplace_back(std::move(key), std::move(value));
            } while (acceptPunct(","));
            return expectPunct("}") ? makeExpr(std::move(map), begin, _pos) : nullptr;
        }

        const std::vector<Token>& _tokens;
        const Preprocessor& _pp;
        std::vector<Diagnostic>& _diagnostics;
        std::size_t _pos = 0;
        bool _panic = false; // inside a construct that hit a syntax error; cleared by recovery
    };

} // namespace

const Procedure* Program::definition(std::string_view name) const {
    const auto it = std::ranges::find_if(procedures, [name](const Procedure& p) { return p.body.has_value() && equalsIgnoreCase(p.name, name); });
    return it == procedures.end() ? nullptr : &*it;
}

Program parse(const std::vector<Token>& tokens, const Preprocessor& preprocessor, std::vector<Diagnostic>& diagnostics) {
    return Parser(tokens, preprocessor, diagnostics).run();
}

} // namespace geck::ssl
