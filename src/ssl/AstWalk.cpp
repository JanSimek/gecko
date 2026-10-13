#include "ssl/AstWalk.h"

namespace geck::ssl {

namespace {

    template <typename... Ts>
    struct Overloaded : Ts... {
        using Ts::operator()...;
    };

    void visitIf(const ExprPtr& expr, const ExprVisitor& visit) {
        if (expr) {
            forEachExpression(*expr, visit);
        }
    }

    void visitIf(const StmtPtr& stmt, const ExprVisitor& visit) {
        if (stmt) {
            forEachExpression(*stmt, visit);
        }
    }

    void visitAll(const std::vector<ExprPtr>& exprs, const ExprVisitor& visit) {
        for (const ExprPtr& expr : exprs) {
            visitIf(expr, visit);
        }
    }

    void visitVariables(const std::vector<Variable>& variables, const ExprVisitor& visit) {
        for (const Variable& variable : variables) {
            visitIf(variable.initializer, visit);
            visitIf(variable.arraySize, visit);
        }
    }

    // The statements directly inside `stmt`.
    std::vector<const Stmt*> children(const Stmt& stmt) {
        std::vector<const Stmt*> out;
        const auto add = [&out](const StmtPtr& child) {
            if (child) {
                out.push_back(child.get());
            }
        };
        std::visit(Overloaded{
                       [&](const Block& node) {
                           for (const StmtPtr& child : node.statements) {
                               add(child);
                           }
                       },
                       [&](const If& node) {
                           add(node.then);
                           add(node.otherwise);
                       },
                       [&](const While& node) { add(node.body); },
                       [&](const For& node) {
                           add(node.init);
                           add(node.step);
                           add(node.body);
                       },
                       [&](const ForEach& node) { add(node.body); },
                       [&](const Switch& node) {
                           for (const SwitchCase& branch : node.cases) {
                               for (const StmtPtr& child : branch.body) {
                                   add(child);
                               }
                           }
                       },
                       [](const auto&) {},
                   },
            stmt.node);
        return out;
    }

    void walkStatements(const Stmt& stmt, const StmtVisitor& visit) {
        visit(stmt);
        for (const Stmt* child : children(stmt)) {
            walkStatements(*child, visit);
        }
    }

} // namespace

void forEachExpression(const Expr& expr, const ExprVisitor& visit) {
    visit(expr);
    std::visit(Overloaded{
                   [&](const Call& node) { visitAll(node.args, visit); },
                   [&](const Unary& node) { visitIf(node.operand, visit); },
                   [&](const Binary& node) {
                       visitIf(node.left, visit);
                       visitIf(node.right, visit);
                   },
                   [&](const Conditional& node) {
                       visitIf(node.whenTrue, visit);
                       visitIf(node.condition, visit);
                       visitIf(node.whenFalse, visit);
                   },
                   [&](const Index& node) {
                       visitIf(node.target, visit);
                       visitIf(node.index, visit);
                   },
                   [&](const Member& node) { visitIf(node.target, visit); },
                   [&](const ArrayLiteral& node) { visitAll(node.elements, visit); },
                   [&](const MapLiteral& node) {
                       for (const auto& [key, value] : node.entries) {
                           visitIf(key, visit);
                           visitIf(value, visit);
                       }
                   },
                   [](const auto&) {},
               },
        expr.node);
}

// The expressions a statement holds itself, then those of the statements nested in it.
void forEachExpression(const Stmt& stmt, const ExprVisitor& visit) {
    std::visit(Overloaded{
                   [&](const LocalVariables& node) { visitVariables(node.variables, visit); },
                   [&](const Assignment& node) {
                       visitIf(node.target, visit);
                       visitIf(node.value, visit);
                   },
                   [&](const ExpressionStatement& node) { visitIf(node.expression, visit); },
                   [&](const If& node) { visitIf(node.condition, visit); },
                   [&](const While& node) { visitIf(node.condition, visit); },
                   [&](const For& node) { visitIf(node.condition, visit); },
                   [&](const ForEach& node) {
                       visitIf(node.collection, visit);
                       visitIf(node.whileCondition, visit);
                   },
                   [&](const Switch& node) {
                       visitIf(node.subject, visit);
                       for (const SwitchCase& branch : node.cases) {
                           visitIf(branch.value, visit);
                       }
                   },
                   [&](const Return& node) { visitIf(node.value, visit); },
                   [&](const CallStatement& node) {
                       visitIf(node.target, visit);
                       visitAll(node.args, visit);
                       visitIf(node.scheduleValue, visit);
                   },
                   [&](const KeywordStatement& node) { visitIf(node.operand, visit); },
                   [](const auto&) {},
               },
        stmt.node);
    for (const Stmt* child : children(stmt)) {
        forEachExpression(*child, visit);
    }
}

void forEachExpression(const Block& block, const ExprVisitor& visit) {
    for (const StmtPtr& stmt : block.statements) {
        visitIf(stmt, visit);
    }
}

void forEachStatement(const Block& block, const StmtVisitor& visit) {
    for (const StmtPtr& stmt : block.statements) {
        if (stmt) {
            walkStatements(*stmt, visit);
        }
    }
}

} // namespace geck::ssl
