#pragma once

#include <vector>

#include "ssl/Ast.h"
#include "ssl/Preprocessor.h"

namespace geck::ssl {

/// Parse a preprocessed token stream into a Program, following sslc's grammar: operator precedence
/// from loosest, `a if c else b`, then and/or/andalso/orelse, one comparison, + - bwand bwor bwxor,
/// * / % div ^, and the unary - not bwnot floor. SSL keywords are case-insensitive.
///
/// A syntax error is a diagnostic at the line the user wrote (through `preprocessor`); the parser
/// then skips to the next `;`, the enclosing `end` or the next declaration and carries on, so a
/// broken procedure does not hide the rest of the program.
Program parse(const std::vector<Token>& tokens, const Preprocessor& preprocessor, std::vector<Diagnostic>& diagnostics);

} // namespace geck::ssl
