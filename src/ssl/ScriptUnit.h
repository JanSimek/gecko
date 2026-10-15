#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "ssl/Ast.h"
#include "ssl/Preprocessor.h"

namespace geck::ssl {

/// One script read the way sslc reads it: its files, the preprocessed tokens, and the syntax tree.
/// It owns everything the tree's token ranges point into, so a unit can be kept and queried as one
/// value (and moved, but not copied).
class ScriptUnit {
public:
    /// Preprocess and parse the script at `path`, following its #includes through `provider`.
    /// Always returns a unit; what went wrong is in diagnostics().
    static ScriptUnit load(const SourceProvider& provider, const std::filesystem::path& path, const PreprocessOptions& options = {});

    const Program& program() const { return _program; }
    const std::vector<Token>& tokens() const { return _tokens; }
    /// Preprocessing diagnostics, then parse diagnostics.
    const std::vector<Diagnostic>& diagnostics() const { return _diagnostics; }
    const SourceManager& sources() const { return *_sources; }
    /// The script file itself, as SourceLocation::file and sources().path() name it; nullopt when it
    /// could not be read.
    std::optional<FileId> scriptFile() const { return _scriptFile; }

    /// Where the user wrote the start of `range`: for code a macro produced, the outermost call site.
    SourceLocation writtenAt(TokenRange range) const;
    /// The macros the first token of `range` came out of, outermost first.
    std::vector<const Macro*> macroChain(TokenRange range) const;
    /// The object-like macro `argument` was written as in `call`, e.g. "AI_ARROYO_VILLAGER" in
    /// `set_self_ai(AI_ARROYO_VILLAGER)`: the outermost macro the argument came out of that the call
    /// itself did not. Empty when it was written as a literal.
    std::string argumentMacro(const Expr& call, const Expr& argument) const;
    /// The integer constant `expr` evaluates to, with the script's macros; nullopt when it is not one.
    std::optional<int64_t> evaluate(const Expr& expr) const;
    /// An object-like macro's value as an integer constant, e.g. `LVAR_Hostile`.
    std::optional<int64_t> evaluate(const Macro& macro) const;
    /// The macros defined at the end of the script, and every #define met on the way, in order.
    const Preprocessor& preprocessor() const { return *_preprocessor; }

private:
    ScriptUnit() = default;

    std::unique_ptr<SourceManager> _sources;
    std::unique_ptr<Preprocessor> _preprocessor;
    std::vector<Token> _tokens;
    Program _program;
    std::vector<Diagnostic> _diagnostics;
    std::optional<FileId> _scriptFile;
};

} // namespace geck::ssl
