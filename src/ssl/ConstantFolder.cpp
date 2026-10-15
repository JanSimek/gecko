#include "ssl/ConstantFolder.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>

#include "ssl/AstWalk.h"

namespace geck::ssl {

namespace {

    using Value = std::optional<int64_t>;

    std::string lower(std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    Value boolean(bool value) {
        return value ? 1 : 0;
    }

    // The variable an assignment or loop writes, lowercased; empty when it writes an array element.
    std::string assignedName(const Stmt& stmt) {
        if (const auto* assignment = std::get_if<Assignment>(&stmt.node); assignment != nullptr && assignment->target) {
            if (const auto* name = std::get_if<Name>(&assignment->target->node)) {
                return lower(name->name);
            }
        }
        return {};
    }

    void collectWritten(const Block& body, std::set<std::string>& written) {
        forEachStatement(body, [&written](const Stmt& stmt) {
            if (std::string name = assignedName(stmt); !name.empty()) {
                written.insert(std::move(name));
            } else if (const auto* loop = std::get_if<ForEach>(&stmt.node)) {
                written.insert(lower(loop->value));
                if (!loop->key.empty()) {
                    written.insert(lower(loop->key));
                }
            }
        });
    }

    using BinaryOp = Value (*)(int64_t, int64_t);

    // The SSL binary operators that fold, by the spelling the parser records (keywords lowercased).
    // '^' (sfall's power) is left unfolded.
    Value applyBinary(std::string_view op, int64_t a, int64_t b) {
        static const std::map<std::string_view, BinaryOp, std::less<>> operators{
            { "+", [](int64_t x, int64_t y) { return Value(x + y); } },
            { "-", [](int64_t x, int64_t y) { return Value(x - y); } },
            { "*", [](int64_t x, int64_t y) { return Value(x * y); } },
            { "/", [](int64_t x, int64_t y) { return y == 0 ? std::nullopt : Value(x / y); } },
            { "div", [](int64_t x, int64_t y) { return y == 0 ? std::nullopt : Value(x / y); } },
            { "%", [](int64_t x, int64_t y) { return y == 0 ? std::nullopt : Value(x % y); } },
            { "==", [](int64_t x, int64_t y) { return boolean(x == y); } },
            { "!=", [](int64_t x, int64_t y) { return boolean(x != y); } },
            { "<", [](int64_t x, int64_t y) { return boolean(x < y); } },
            { ">", [](int64_t x, int64_t y) { return boolean(x > y); } },
            { "<=", [](int64_t x, int64_t y) { return boolean(x <= y); } },
            { ">=", [](int64_t x, int64_t y) { return boolean(x >= y); } },
            { "and", [](int64_t x, int64_t y) { return boolean(x != 0 && y != 0); } },
            { "andalso", [](int64_t x, int64_t y) { return boolean(x != 0 && y != 0); } },
            { "or", [](int64_t x, int64_t y) { return boolean(x != 0 || y != 0); } },
            { "orelse", [](int64_t x, int64_t y) { return boolean(x != 0 || y != 0); } },
            { "bwand", [](int64_t x, int64_t y) { return Value(x & y); } },
            { "bwor", [](int64_t x, int64_t y) { return Value(x | y); } },
            { "bwxor", [](int64_t x, int64_t y) { return Value(x ^ y); } },
        };
        const auto it = operators.find(op);
        return it == operators.end() ? std::nullopt : it->second(a, b);
    }

    Value applyUnary(std::string_view op, int64_t operand) {
        if (op == "-") {
            return -operand;
        }
        if (op == "not") {
            return boolean(operand == 0);
        }
        if (op == "bwnot") {
            return ~operand;
        }
        if (op == "floor") {
            return operand; // already an integer
        }
        return std::nullopt;
    }

} // namespace

ConstantFolder::ConstantFolder(const Program& program) {
    std::set<std::string> written;
    for (const Procedure& procedure : program.procedures) {
        if (procedure.body.has_value()) {
            collectWritten(*procedure.body, written);
        }
    }
    for (const GlobalVariables& globals : program.variables) {
        if (globals.linkage != Linkage::Local) {
            continue;
        }
        for (const Variable& variable : globals.variables) {
            const std::string name = lower(variable.name);
            if (written.contains(name) || variable.arraySize) {
                continue;
            }
            const Value initial = variable.initializer ? fold(*variable.initializer) : Value(0);
            if (initial.has_value()) {
                _constants.insert_or_assign(name, *initial);
            }
        }
    }
}

std::optional<int64_t> ConstantFolder::fold(const Expr& expr, const std::set<std::string>& shadowed) const {
    if (const auto* literal = std::get_if<IntLiteral>(&expr.node)) {
        return literal->value;
    }
    if (const auto* name = std::get_if<Name>(&expr.node)) {
        const std::string lowered = lower(name->name);
        if (lowered == "true" || lowered == "false") {
            return boolean(lowered == "true"); // sslc lex.c built-in constants
        }
        const auto it = shadowed.contains(lowered) ? _constants.end() : _constants.find(lowered);
        return it == _constants.end() ? std::nullopt : Value(it->second);
    }
    if (const auto* unary = std::get_if<Unary>(&expr.node)) {
        const Value operand = unary->operand ? fold(*unary->operand, shadowed) : std::nullopt;
        if (!operand.has_value()) {
            return std::nullopt;
        }
        return applyUnary(unary->op, *operand);
    }
    if (const auto* binary = std::get_if<Binary>(&expr.node)) {
        const Value left = binary->left ? fold(*binary->left, shadowed) : std::nullopt;
        const Value right = binary->right ? fold(*binary->right, shadowed) : std::nullopt;
        if (!left.has_value() || !right.has_value()) {
            return std::nullopt;
        }
        return applyBinary(binary->op, *left, *right);
    }
    if (const auto* conditional = std::get_if<Conditional>(&expr.node)) {
        const Value condition = conditional->condition ? fold(*conditional->condition, shadowed) : std::nullopt;
        if (!condition.has_value()) {
            return std::nullopt;
        }
        const ExprPtr& chosen = *condition != 0 ? conditional->whenTrue : conditional->whenFalse;
        return chosen ? fold(*chosen, shadowed) : std::nullopt;
    }
    return std::nullopt;
}

} // namespace geck::ssl
