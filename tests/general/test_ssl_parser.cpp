#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ssl/AstWalk.h"
#include "ssl/ScriptUnit.h"

#include "support/SslParse.h"

using namespace geck::ssl;
using geck::test::parseSsl;

namespace {

// An expression as a fully parenthesised string, to check how it was grouped.
std::string show(const Expr& expr);

std::string showAll(const std::vector<ExprPtr>& exprs) {
    std::string out;
    for (const ExprPtr& e : exprs) {
        out += (out.empty() ? "" : ", ") + show(*e);
    }
    return out;
}

std::string show(const Expr& expr) {
    if (const auto* n = std::get_if<IntLiteral>(&expr.node)) {
        return std::to_string(n->value);
    }
    if (const auto* n = std::get_if<StringLiteral>(&expr.node)) {
        return "\"" + n->value + "\"";
    }
    if (const auto* n = std::get_if<Name>(&expr.node)) {
        return n->name;
    }
    if (const auto* n = std::get_if<ProcedureRef>(&expr.node)) {
        return "@" + n->name;
    }
    if (const auto* n = std::get_if<Call>(&expr.node)) {
        return n->callee + "(" + showAll(n->args) + ")";
    }
    if (const auto* n = std::get_if<Unary>(&expr.node)) {
        return "(" + n->op + " " + show(*n->operand) + ")";
    }
    if (const auto* n = std::get_if<Binary>(&expr.node)) {
        return "(" + show(*n->left) + " " + n->op + " " + show(*n->right) + ")";
    }
    if (const auto* n = std::get_if<Conditional>(&expr.node)) {
        return "(" + show(*n->whenTrue) + " if " + show(*n->condition) + " else " + show(*n->whenFalse) + ")";
    }
    if (const auto* n = std::get_if<Index>(&expr.node)) {
        return show(*n->target) + "[" + show(*n->index) + "]";
    }
    if (const auto* n = std::get_if<Member>(&expr.node)) {
        return show(*n->target) + "." + n->member;
    }
    if (const auto* n = std::get_if<ArrayLiteral>(&expr.node)) {
        return "[" + showAll(n->elements) + "]";
    }
    return "?";
}

// The expression `x := <text>;` assigns, inside a procedure.
std::string grouping(const std::string& text) {
    const ScriptUnit unit = parseSsl("procedure p begin x := " + text + "; end");
    REQUIRE(unit.diagnostics().empty());
    const auto& statement = unit.program().procedures.at(0).body->statements.at(0);
    return show(*std::get<Assignment>(statement->node).value);
}

} // namespace

TEST_CASE("Parser groups operators with sslc's precedence", "[ssl_parser][ssl]") {
    CHECK(grouping("1 + 2 * 3") == "(1 + (2 * 3))");
    CHECK(grouping("a - b - c") == "((a - b) - c)");
    CHECK(grouping("a == 1 and b != 2 or c") == "(((a == 1) and (b != 2)) or c)");
    CHECK(grouping("not a == b") == "((not a) == b)"); // unary binds tighter than a comparison
    CHECK(grouping("a bwand b + c div 2") == "((a bwand b) + (c div 2))");
    CHECK(grouping("-x * floor y") == "((- x) * (floor y))");
    CHECK(grouping("1 if a > 2 else 3") == "(1 if (a > 2) else 3)");
    CHECK(grouping("f(a, g(b))[1].name") == "f(a, g(b))[1].name");
    CHECK(grouping("[1, \"two\\n\", @proc]") == "[1, \"two\n\", @proc]");
    CHECK(grouping("A AND B") == "(A and B)"); // keywords are case-insensitive
}

TEST_CASE("Parser reads declarations: procedures, forward declarations, variables, imports", "[ssl_parser][ssl]") {
    const ScriptUnit unit = parseSsl(R"ssl(
variable counter := 0;
variable begin first; second := 2; end
import variable shared_list;
export procedure helper;
procedure start;
procedure helper(variable a, variable b = 1,) begin end
critical procedure every_second in 10 begin end
procedure on_hostile when (hostile) begin end
procedure start begin
   variable i = 0, cells[10];
end
)ssl");
    REQUIRE(unit.diagnostics().empty());
    const Program& program = unit.program();
    REQUIRE(program.procedures.size() == 6);
    CHECK(program.procedures[0].linkage == Linkage::Export);
    CHECK_FALSE(program.procedures[1].body.has_value()); // forward declaration
    const Procedure* helper = program.definition("HELPER");
    REQUIRE(helper != nullptr);
    REQUIRE(helper->parameters.size() == 2); // the trailing comma is allowed, as sslc does
    CHECK(helper->parameters[1].defaultValue != nullptr);
    CHECK(program.definition("every_second")->critical);
    CHECK(program.definition("every_second")->timedDelay != nullptr);
    CHECK(program.definition("on_hostile")->condition != nullptr);
    REQUIRE(program.variables.size() == 3);
    CHECK(program.variables[1].variables.size() == 2);
    CHECK(program.variables[2].linkage == Linkage::Import);
    const auto& locals = std::get<LocalVariables>(program.definition("start")->body->statements.at(0)->node);
    REQUIRE(locals.variables.size() == 2);
    CHECK(locals.variables[1].arraySize != nullptr);
}

TEST_CASE("Parser reads every statement form", "[ssl_parser][ssl]") {
    const ScriptUnit unit = parseSsl(R"ssl(
procedure p begin
   if (a) then b := 1; else begin c += 2; end
   while (i < 10) do i++;
   for (i := 0; i < 3; i++) begin end
   foreach (variable key: value in list while key < 5) begin end
   switch (x) begin
      case 1: y := 1;
      case 2:
      default: y := 0;
   end
   call helper(1) in 5;
   call "dynamic";
   list[0] := 3;
   obj.name = "x";
   display_msg("hi");
   return x;
   break;
   ;
end
)ssl");
    REQUIRE(unit.diagnostics().empty());
    const auto& statements = unit.program().procedures.at(0).body->statements;
    REQUIRE(statements.size() == 13);
    CHECK(std::holds_alternative<If>(statements[0]->node));
    CHECK(std::get<If>(statements[0]->node).otherwise != nullptr);
    CHECK(std::holds_alternative<While>(statements[1]->node));
    CHECK(std::holds_alternative<For>(statements[2]->node));
    const auto& loop = std::get<ForEach>(statements[3]->node);
    CHECK(loop.declares);
    CHECK(loop.key == "key");
    CHECK(loop.whileCondition != nullptr);
    CHECK(std::get<Switch>(statements[4]->node).cases.size() == 3);
    const auto& call = std::get<CallStatement>(statements[5]->node);
    CHECK(call.schedule == "in");
    CHECK(std::holds_alternative<StringLiteral>(std::get<CallStatement>(statements[6]->node).target->node));
    CHECK(std::holds_alternative<Index>(std::get<Assignment>(statements[7]->node).target->node));
    CHECK(std::get<Assignment>(statements[8]->node).op == ":="); // '=' assigns, as sslc lexes it
    CHECK(std::holds_alternative<ExpressionStatement>(statements[9]->node));
    CHECK(std::holds_alternative<Return>(statements[10]->node));
    CHECK(std::get<KeywordStatement>(statements[11]->node).keyword == "break");
    CHECK(std::holds_alternative<Empty>(statements[12]->node));
}

TEST_CASE("Parser reports a syntax error where it was written and reads on", "[ssl_parser][ssl]") {
    const ScriptUnit unit = parseSsl(R"ssl(
procedure broken begin
   x := ;
   y := 2;
end
procedure fine begin
   z := 3;
end
)ssl");
    REQUIRE(unit.diagnostics().size() == 1);
    CHECK(unit.diagnostics()[0].location.line == 3);
    CHECK(unit.diagnostics()[0].message == "expected an expression, found ';'");
    REQUIRE(unit.program().procedures.size() == 2);
    CHECK(unit.program().procedures[0].body->statements.size() == 1); // y := 2 survives the bad line
    CHECK(unit.program().definition("fine") != nullptr);
}

TEST_CASE("Parser locates code a macro wrote at the line that used the macro", "[ssl_parser][ssl]") {
    const ScriptUnit unit = parseSsl("#define give(x) give_exp_points(x)\n#define REWARD (100)\nprocedure p begin\n   give(REWARD);\nend\n");
    REQUIRE(unit.diagnostics().empty());
    const auto& statement = *unit.program().procedures.at(0).body->statements.at(0);
    const Expr& call = *std::get<ExpressionStatement>(statement.node).expression;
    CHECK(std::get<Call>(call.node).callee == "give_exp_points");
    CHECK(unit.writtenAt(call.range).line == 4);
    CHECK(unit.evaluate(*std::get<Call>(call.node).args.at(0)) == 100);
}

TEST_CASE("forEachExpression visits nested expressions and statements", "[ssl_parser][ssl]") {
    const ScriptUnit unit = parseSsl(R"ssl(
procedure p begin
   if (a(1)) then begin
      while (b) do c(d(2));
   end
end
)ssl");
    REQUIRE(unit.diagnostics().empty());
    std::vector<std::string> calls;
    forEachExpression(*unit.program().procedures.at(0).body, [&calls](const Expr& expr) {
        if (const auto* call = std::get_if<Call>(&expr.node)) {
            calls.push_back(call->callee);
        }
    });
    CHECK(calls == std::vector<std::string>{ "a", "c", "d" });
}
