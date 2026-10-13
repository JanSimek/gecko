#include "ssl/Lexer.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace geck::ssl {

namespace {

    // Longest first, so ":=" is not read as ":" "=".
    constexpr std::array<std::string_view, 21> MULTI_CHAR_PUNCT{ "...", "<<=", ">>=", "##", ":=", "==", "!=",
        "<=", ">=", "+=", "-=", "*=", "/=", "%=", "++", "--", "&&", "||", "<<", ">>", "->" };

    bool isIdentStart(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }

    bool isIdentChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    bool isDigit(char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    }

    class Scanner {
    public:
        Scanner(std::string_view text, FileId file, std::vector<Diagnostic>& diagnostics)
            : _text(text)
            , _file(file)
            , _diagnostics(diagnostics) {}

        std::vector<Token> run() {
            std::vector<Token> tokens;
            while (true) {
                skipBlanks();
                Token token;
                token.location = location();
                token.atLineStart = _atLineStart;
                token.spaceBefore = _spaceBefore;
                if (_pos >= _text.size()) {
                    token.kind = TokenKind::EndOfFile;
                    tokens.push_back(token);
                    return tokens;
                }
                const std::size_t start = _pos;
                token.kind = scanToken();
                token.text = _text.substr(start, _pos - start);
                tokens.push_back(token);
                _atLineStart = false;
                _spaceBefore = false;
            }
        }

    private:
        char peek(std::size_t ahead = 0) const {
            return _pos + ahead < _text.size() ? _text[_pos + ahead] : '\0';
        }

        void advance() {
            if (_text[_pos] == '\n') {
                ++_line;
                _lineStart = _pos + 1;
            }
            ++_pos;
        }

        SourceLocation location() const {
            return { _file, _line, static_cast<uint32_t>(_pos - _lineStart + 1) };
        }

        void report(std::string message) {
            _diagnostics.emplace_back(Diagnostic::Severity::Error, location(), std::move(message));
        }

        // A backslash followed only by blanks up to the end of the line: the length to skip, else 0.
        std::size_t lineSplice() const {
            if (peek() != '\\') {
                return 0;
            }
            std::size_t ahead = 1;
            while (peek(ahead) == ' ' || peek(ahead) == '\t' || peek(ahead) == '\r') {
                ++ahead;
            }
            return peek(ahead) == '\n' ? ahead + 1 : 0;
        }

        void skipBlanks() {
            while (_pos < _text.size()) {
                const char c = peek();
                if (const std::size_t splice = lineSplice()) {
                    for (std::size_t i = 0; i < splice; ++i) {
                        advance();
                    }
                    _spaceBefore = true;
                } else if (c == '\n') {
                    advance();
                    _atLineStart = true;
                    _spaceBefore = false;
                } else if (std::isspace(static_cast<unsigned char>(c))) {
                    advance();
                    _spaceBefore = true;
                } else if (c == '/' && peek(1) == '/') {
                    while (_pos < _text.size() && peek() != '\n') {
                        advance();
                    }
                } else if (c == '/' && peek(1) == '*') {
                    skipBlockComment();
                } else {
                    return;
                }
            }
        }

        void skipBlockComment() {
            const SourceLocation opened = location();
            advance();
            advance();
            while (_pos < _text.size() && !(peek() == '*' && peek(1) == '/')) {
                advance();
            }
            if (_pos >= _text.size()) {
                _diagnostics.emplace_back(Diagnostic::Severity::Error, opened, "unterminated block comment");
                return;
            }
            advance();
            advance();
            _spaceBefore = true;
        }

        TokenKind scanToken() {
            const char c = peek();
            if (isIdentStart(c)) {
                while (isIdentChar(peek())) {
                    advance();
                }
                return TokenKind::Identifier;
            }
            if (isDigit(c) || (c == '.' && isDigit(peek(1)))) {
                scanNumber();
                return TokenKind::Number;
            }
            if (c == '"' || c == '\'') {
                return scanQuoted(c);
            }
            const auto multi = std::ranges::find_if(MULTI_CHAR_PUNCT, [this](std::string_view punct) { return _text.substr(_pos, punct.size()) == punct; });
            const std::size_t length = multi != MULTI_CHAR_PUNCT.end() ? multi->size() : 1;
            for (std::size_t i = 0; i < length; ++i) {
                advance();
            }
            return TokenKind::Punct;
        }

        // A preprocessing number: a digit, then letters, digits, '_', '.', and a sign after an exponent.
        void scanNumber() {
            advance();
            while (true) {
                const char c = peek();
                if ((c == '+' || c == '-') && (_text[_pos - 1] == 'e' || _text[_pos - 1] == 'E')) {
                    advance();
                } else if (isIdentChar(c) || c == '.') {
                    advance();
                } else {
                    return;
                }
            }
        }

        TokenKind scanQuoted(char quote) {
            advance();
            while (_pos < _text.size() && peek() != quote && peek() != '\n') {
                if (peek() == '\\' && _pos + 1 < _text.size() && peek(1) != '\n') {
                    advance();
                }
                advance();
            }
            if (peek() == quote) {
                advance();
            } else {
                report(quote == '"' ? "unterminated string" : "unterminated character literal");
            }
            return quote == '"' ? TokenKind::String : TokenKind::Char;
        }

        std::string_view _text;
        FileId _file;
        std::vector<Diagnostic>& _diagnostics;
        std::size_t _pos = 0;
        std::size_t _lineStart = 0;
        uint32_t _line = 1;
        bool _atLineStart = true;
        bool _spaceBefore = false;
    };

} // namespace

std::vector<Token> lex(std::string_view text, FileId file, std::vector<Diagnostic>& diagnostics) {
    return Scanner(text, file, diagnostics).run();
}

} // namespace geck::ssl
