#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ssl/Lexer.h"
#include "ssl/Source.h"

namespace geck::ssl {

struct Macro {
    std::string_view name;
    bool functionLike = false;
    bool variadic = false;                ///< takes `...`, read back as __VA_ARGS__
    std::vector<std::string_view> params; ///< named parameters, without __VA_ARGS__
    std::vector<Token> body;
    SourceLocation location; ///< where the #define is
};

/// One macro expansion: which macro, and the token that invoked it. A call written inside another
/// macro's body has that expansion as its parent, so the chain leads back to the line in a file.
struct Expansion {
    const Macro* macro = nullptr;
    SourceLocation callSite;
    ExpansionId parent = 0;
};

struct PreprocessOptions {
    /// Searched in order after the including file's own directory, as sslc's -I.
    std::vector<std::filesystem::path> includeDirs;
    int maxIncludeDepth = 200;
};

/// The C preprocessor sslc runs (mcpp) for SSL: #include, object- and function-like #define with
/// `#`, `##` and __VA_ARGS__, #undef, and #if/#ifdef/#ifndef/#elif/#else/#endif. Macro expansion
/// follows the C standard's rescanning rules (each token carries the set of macros it may no longer
/// expand). Problems are diagnostics; a run always produces tokens.
///
/// Tokens view text owned by the SourceManager and by this object, so keep both alive while using them.
class Preprocessor {
public:
    explicit Preprocessor(SourceManager& sources, PreprocessOptions options = {});

    /// The token stream of the file at `path` after preprocessing: what sslc's parser reads. Empty,
    /// with a diagnostic, when the file can't be read.
    std::vector<Token> run(const std::filesystem::path& path);

    const std::vector<Diagnostic>& diagnostics() const { return _diagnostics; }
    SourceManager& sources() const { return _sources; }

    /// Every #define processed, in order, including ones later #undef'd or redefined.
    const std::deque<Macro>& definitions() const { return _definitions; }
    /// The macro `name` names now (at the end of a run); nullptr when undefined.
    const Macro* find(std::string_view name) const;

    /// Where the user wrote `token`: its own location, or for a token a macro produced, the outermost
    /// call site.
    SourceLocation writtenAt(const Token& token) const;
    /// The macros `token` came out of, outermost first; empty for a token written in a file.
    std::vector<const Macro*> macroChain(const Token& token) const;

    /// Expand `tokens` with the macros defined now and evaluate them as an integer constant expression
    /// (C operators and precedence, and sslc's `true` and `false`). nullopt when another name is left
    /// or the expression is malformed.
    std::optional<int64_t> evaluate(const std::vector<Token>& tokens);
    /// An object-like macro's value as an integer constant expression.
    std::optional<int64_t> evaluate(const Macro& macro);

private:
    struct Frame {
        FileId file = 0;
        const std::vector<Token>* tokens = nullptr;
        std::size_t pos = 0;
        std::size_t conditionalDepth = 0; // _conditionals.size() when the file was entered
    };
    struct Conditional {
        bool active = false;
        bool taken = false; // some branch of this #if has been active
        bool sawElse = false;
        bool parentActive = false;
        SourceLocation location;
    };

    struct Call {
        std::vector<std::vector<Token>> args;
        Token close; // the ')'
    };
    struct Substitution {
        const Macro& macro;
        const std::vector<std::vector<Token>>& args;
        ExpansionId expansion;
        std::map<std::size_t, std::vector<Token>> expandedArgs;
    };

    bool active() const;
    bool pushFile(FileId file, SourceLocation includedAt);
    void popFile();
    Token nextRaw();
    Token next();
    std::vector<Token> expandList(const std::vector<Token>& tokens);
    bool expand(const Token& name, const Macro& macro);
    std::optional<Call> readCall(const Token& name, const Macro& macro);
    void checkArity(const Token& name, const Macro& macro, std::vector<std::vector<Token>>& args);
    std::vector<Token> substitute(const Macro& macro, const std::vector<std::vector<Token>>& args, ExpansionId expansion);
    std::vector<Token> operand(Substitution& sub, std::size_t i, bool raw);
    void appendPasted(std::vector<Token>& out, const std::vector<Token>& right, ExpansionId expansion);
    Token paste(const Token& left, const Token& right);
    Token stringize(const std::vector<Token>& arg);
    std::string_view keep(std::string text);

    std::vector<Token> directiveLine();
    void directive(const Token& hash);
    bool conditionalDirective(std::string_view kind, const std::vector<Token>& line, const Token& hash);
    void include(const std::vector<Token>& written, const Token& directive);
    void define(const std::vector<Token>& line, const Token& directive);
    bool evaluateCondition(const std::vector<Token>& line);
    void report(Diagnostic::Severity severity, SourceLocation location, std::string message);

    bool hidden(HideSetId set, std::string_view name) const;
    HideSetId withName(HideSetId set, std::string_view name);
    HideSetId unite(HideSetId a, HideSetId b);
    HideSetId intersect(HideSetId a, HideSetId b);
    HideSetId intern(std::vector<std::string_view> names);

    SourceManager& _sources;
    PreprocessOptions _options;
    std::vector<Diagnostic> _diagnostics;

    std::map<FileId, std::vector<Token>> _lexed; // each file lexed once, however often it is included
    std::vector<Frame> _frames;
    std::deque<Token> _pending; // expansion output, read before the next file token
    int _isolated = 0;          // >0 while expanding a token list on its own (an argument, an #if line)
    std::vector<Conditional> _conditionals;

    std::deque<Macro> _definitions; // deque: Expansion and _active point into it
    std::map<std::string_view, const Macro*, std::less<>> _active;
    std::vector<Expansion> _expansions;
    std::vector<std::vector<std::string_view>> _hideSets;
    std::map<std::vector<std::string_view>, HideSetId> _hideSetIds;
    std::vector<std::unique_ptr<std::string>> _synthesized; // text of pasted and stringized tokens
};

} // namespace geck::ssl
