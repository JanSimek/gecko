#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/// The syntax tree of a preprocessed SSL program, shaped after sslc's grammar (parse.c, parseext.c).
///
/// Names are not resolved: sslc tells a variable, a procedure and an engine opcode apart with its
/// symbol and opcode tables, which the analysis passes bring; here `foo` is a Name and `foo(x)` a
/// Call whatever `foo` turns out to be. Every node records the range of preprocessed tokens it was
/// parsed from, so a pass can ask the Preprocessor where it was written and which macros produced it.
namespace geck::ssl {

/// Half-open range of indices into the preprocessed token stream.
struct TokenRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct IntLiteral {
    int64_t value = 0;
};
struct FloatLiteral {
    double value = 0;
};
struct StringLiteral {
    std::string value; ///< with escapes resolved
};
/// A variable, constant or argument-less opcode (`self_obj`), unresolved.
struct Name {
    std::string name;
};
/// `@name`: a procedure passed by reference.
struct ProcedureRef {
    std::string name;
};
/// `name(args)`: an opcode, a procedure or a call through a variable, unresolved.
struct Call {
    std::string callee;
    std::vector<ExprPtr> args;
};
struct Unary {
    std::string op; ///< "-", "not", "bwnot", "floor" (keywords lowercased)
    ExprPtr operand;
};
struct Binary {
    std::string op; ///< as written, keywords lowercased: "+", "and", "bwor", "==", ...
    ExprPtr left;
    ExprPtr right;
};
/// sfall's `a if condition else b`.
struct Conditional {
    ExprPtr whenTrue;
    ExprPtr condition;
    ExprPtr whenFalse;
};
/// `target[index]`.
struct Index {
    ExprPtr target;
    ExprPtr index;
};
/// `target.member`, sfall's shorthand for target["member"].
struct Member {
    ExprPtr target;
    std::string member;
};
struct ArrayLiteral {
    std::vector<ExprPtr> elements;
};
struct MapLiteral {
    std::vector<std::pair<ExprPtr, ExprPtr>> entries;
};

struct Expr {
    std::variant<IntLiteral, FloatLiteral, StringLiteral, Name, ProcedureRef, Call, Unary, Binary, Conditional, Index,
        Member, ArrayLiteral, MapLiteral>
        node;
    TokenRange range;
};

/// One declared variable: `name`, `name := init` or `name[size]`.
struct Variable {
    std::string name;
    ExprPtr initializer;
    ExprPtr arraySize; ///< `name[size]`, a temporary array
    TokenRange range;
};

struct Block {
    std::vector<StmtPtr> statements;
};
/// `variable a, b := 1;` inside a procedure.
struct LocalVariables {
    std::vector<Variable> variables;
};
/// `target := value` (also written `target = value`), `target += value` (also -=, *=, /=), or
/// `target++` / `target--` (no value).
struct Assignment {
    ExprPtr target; ///< a Name, Index or Member
    std::string op;
    ExprPtr value;
};
/// An expression run for its effect, normally an opcode call: `display_msg(..);`.
struct ExpressionStatement {
    ExprPtr expression;
};
struct If {
    ExprPtr condition;
    StmtPtr then;
    StmtPtr otherwise; ///< null without `else`
};
struct While {
    ExprPtr condition;
    StmtPtr body;
};
/// sfall `for (init; condition; step) body`.
struct For {
    StmtPtr init;
    ExprPtr condition;
    StmtPtr step;
    StmtPtr body;
};
/// sfall `foreach (key: value in collection while condition) body`.
struct ForEach {
    bool declares = false; ///< `foreach (variable v in ...)`
    std::string key;       ///< empty without `key:`
    std::string value;
    ExprPtr collection;
    ExprPtr whileCondition; ///< null without `while`
    StmtPtr body;
};
struct SwitchCase {
    ExprPtr value; ///< null for `default`
    std::vector<StmtPtr> body;
};
/// sfall `switch subject begin case v: ... default: ... end`.
struct Switch {
    ExprPtr subject;
    std::vector<SwitchCase> cases;
};
struct Return {
    ExprPtr value; ///< null for a bare `return`
};
/// `call target(args)`, optionally `in delay` or `when condition`.
struct CallStatement {
    ExprPtr target; ///< the procedure: a Name, or a string or variable naming one
    std::vector<ExprPtr> args;
    std::string schedule; ///< "", "in" or "when"
    ExprPtr scheduleValue;
};
/// The keyword statements: break, continue, exit, detach, noop, cancelall, startcritical,
/// endcritical, and cancel(proc), wait/fork/spawn/exec/callstart with their one operand.
struct KeywordStatement {
    std::string keyword; ///< lowercased
    ExprPtr operand;     ///< null where the keyword takes none
};
/// A lone `;`.
struct Empty {};

struct Stmt {
    std::variant<Block, LocalVariables, Assignment, ExpressionStatement, If, While, For, ForEach, Switch, Return,
        CallStatement, KeywordStatement, Empty>
        node;
    TokenRange range;
};

enum class Linkage {
    Local,  ///< defined in this program
    Import, ///< `import`: defined by another script
    Export, ///< `export`: shared with other scripts
};

struct Parameter {
    std::string name;
    ExprPtr defaultValue; ///< sfall optional argument
};

struct Procedure {
    std::string name;
    std::vector<Parameter> parameters;
    Linkage linkage = Linkage::Local;
    bool critical = false;
    bool pure = false;
    bool inlined = false;
    ExprPtr timedDelay;        ///< `procedure p in N`
    ExprPtr condition;         ///< `procedure p when condition`
    std::optional<Block> body; ///< nullopt for a forward declaration or an import
    TokenRange range;
};

struct GlobalVariables {
    Linkage linkage = Linkage::Local;
    std::vector<Variable> variables;
};

/// A whole program: declarations in source order (includes are already expanded).
struct Program {
    std::vector<Procedure> procedures;
    std::vector<GlobalVariables> variables;

    /// The procedure with a body named `name` (case-insensitive, as sslc), or nullptr.
    const Procedure* definition(std::string_view name) const;
};

} // namespace geck::ssl
