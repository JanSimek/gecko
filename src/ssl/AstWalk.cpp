#include "ssl/AstWalk.h"

namespace geck::ssl {

namespace {

    template <typename... Ts>
    struct Overloaded : Ts... {
        using Ts::operator()...;
    };

    void visitIf(const Expr* expr, const ExprVisitor& visit) {
        if (expr) {
            forEachExpression(*expr, visit);
        }
    }

    void visitIf(const Stmt* stmt, const ExprVisitor& visit) {
        if (stmt) {
            forEachExpression(*stmt, visit);
        }
    }

    void visitAll(const std::vector<ExprPtr>& exprs, const ExprVisitor& visit) {
        for (const ExprPtr& expr : exprs) {
            visitIf(expr.get(), visit);
        }
    }

    void visitVariables(const std::vector<Variable>& variables, const ExprVisitor& visit) {
        for (const Variable& variable : variables) {
            visitIf(variable.initializer.get(), visit);
            visitIf(variable.arraySize.get(), visit);
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
                       [&add](const Block& node) {
                           for (const StmtPtr& child : node.statements) {
                               add(child);
                           }
                       },
                       [&add](const If& node) {
                           add(node.then);
                           add(node.otherwise);
                       },
                       [&add](const While& node) { add(node.body); },
                       [&add](const For& node) {
                           add(node.init);
                           add(node.step);
                           add(node.body);
                       },
                       [&add](const ForEach& node) { add(node.body); },
                       [&add](const Switch& node) {
                           for (const SwitchCase& branch : node.cases) {
                               for (const StmtPtr& child : branch.body) {
                                   add(child);
                               }
                           }
                       },
                       [](const auto&) { /* the other kinds hold none */ },
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
                   [&visit](const Call& node) { visitAll(node.args, visit); },
                   [&visit](const Unary& node) { visitIf(node.operand.get(), visit); },
                   [&visit](const Binary& node) {
                       visitIf(node.left.get(), visit);
                       visitIf(node.right.get(), visit);
                   },
                   [&visit](const Conditional& node) {
                       visitIf(node.whenTrue.get(), visit);
                       visitIf(node.condition.get(), visit);
                       visitIf(node.whenFalse.get(), visit);
                   },
                   [&visit](const Index& node) {
                       visitIf(node.target.get(), visit);
                       visitIf(node.index.get(), visit);
                   },
                   [&visit](const Member& node) { visitIf(node.target.get(), visit); },
                   [&visit](const ArrayLiteral& node) { visitAll(node.elements, visit); },
                   [&visit](const MapLiteral& node) {
                       for (const auto& [key, value] : node.entries) {
                           visitIf(key.get(), visit);
                           visitIf(value.get(), visit);
                       }
                   },
                   [](const auto&) { /* the other kinds hold none */ },
               },
        expr.node);
}

// The expressions a statement holds itself, then those of the statements nested in it.
void forEachExpression(const Stmt& stmt, const ExprVisitor& visit) {
    std::visit(Overloaded{
                   [&visit](const LocalVariables& node) { visitVariables(node.variables, visit); },
                   [&visit](const Assignment& node) {
                       visitIf(node.target.get(), visit);
                       visitIf(node.value.get(), visit);
                   },
                   [&visit](const ExpressionStatement& node) { visitIf(node.expression.get(), visit); },
                   [&visit](const If& node) { visitIf(node.condition.get(), visit); },
                   [&visit](const While& node) { visitIf(node.condition.get(), visit); },
                   [&visit](const For& node) { visitIf(node.condition.get(), visit); },
                   [&visit](const ForEach& node) {
                       visitIf(node.collection.get(), visit);
                       visitIf(node.whileCondition.get(), visit);
                   },
                   [&visit](const Switch& node) {
                       visitIf(node.subject.get(), visit);
                       for (const SwitchCase& branch : node.cases) {
                           visitIf(branch.value.get(), visit);
                       }
                   },
                   [&visit](const Return& node) { visitIf(node.value.get(), visit); },
                   [&visit](const CallStatement& node) {
                       visitIf(node.target.get(), visit);
                       visitAll(node.args, visit);
                       visitIf(node.scheduleValue.get(), visit);
                   },
                   [&visit](const KeywordStatement& node) { visitIf(node.operand.get(), visit); },
                   [](const auto&) { /* the other kinds hold none */ },
               },
        stmt.node);
    for (const Stmt* child : children(stmt)) {
        forEachExpression(*child, visit);
    }
}

void forEachExpression(const Block& block, const ExprVisitor& visit) {
    for (const StmtPtr& stmt : block.statements) {
        visitIf(stmt.get(), visit);
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
