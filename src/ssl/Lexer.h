#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "ssl/Source.h"

namespace geck::ssl {

using ExpansionId = uint32_t; ///< index into Preprocessor's expansions; 0 = written in a file, not expanded
using HideSetId = uint32_t;   ///< index into Preprocessor's hide sets; 0 = empty

enum class TokenKind : uint8_t {
    Identifier,
    Number, ///< a preprocessing number: decimal, hex or float as written
    String, ///< including its quotes; escapes left as written
    Char,   ///< a '...' literal, including its quotes
    Punct,  ///< an operator or separator, e.g. ":=", "(", "##"
    EndOfFile,
};

/// One preprocessing token. `text` views the SourceManager's file text, or text the preprocessor
/// synthesized (a pasted or stringized token).
struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string_view text;
    SourceLocation location;
    bool atLineStart = false; ///< first token on its logical line (backslash-newline joins lines)
    bool spaceBefore = false;
    ExpansionId expansion = 0;
    HideSetId hideSet = 0;

    bool is(std::string_view punct) const { return kind == TokenKind::Punct && text == punct; }
    bool isIdentifier(std::string_view name) const { return kind == TokenKind::Identifier && text == name; }
};

/// Split `text` into preprocessing tokens the way mcpp, which sslc runs first, does: `//` and `/* */`
/// comments are whitespace, a backslash at the end of a line (trailing blanks allowed) joins it to the
/// next, and an unterminated string or comment is reported, not fatal. The last token is EndOfFile.
std::vector<Token> lex(std::string_view text, FileId file, std::vector<Diagnostic>& diagnostics);

} // namespace geck::ssl
