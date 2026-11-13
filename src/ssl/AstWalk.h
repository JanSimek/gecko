#pragma once

#include <functional>

#include "ssl/Ast.h"

namespace geck::ssl {

using ExprVisitor = std::function<void(const Expr&)>;
using StmtVisitor = std::function<void(const Stmt&)>;

/// Every expression in `expr`, `stmt` or `block`, depth-first, each before the expressions inside it.
void forEachExpression(const Expr& expr, const ExprVisitor& visit);
void forEachExpression(const Stmt& stmt, const ExprVisitor& visit);
void forEachExpression(const Block& block, const ExprVisitor& visit);

/// Every statement in `block`, depth-first, each before the statements nested in it.
void forEachStatement(const Block& block, const StmtVisitor& visit);

/// The expressions `stmt` holds itself, not those of the statements nested in it.
void forEachDirectExpression(const Stmt& stmt, const ExprVisitor& visit);
/// The statements directly nested in `stmt`: a block's statements, a branch, a loop body.
void forEachChildStatement(const Stmt& stmt, const StmtVisitor& visit);

} // namespace geck::ssl
