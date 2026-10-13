#include "ssl/ScriptUnit.h"

#include "ssl/Parser.h"

namespace geck::ssl {

ScriptUnit ScriptUnit::load(const SourceProvider& provider, const std::filesystem::path& path, const PreprocessOptions& options) {
    ScriptUnit unit;
    unit._sources = std::make_unique<SourceManager>(provider);
    unit._preprocessor = std::make_unique<Preprocessor>(*unit._sources, options);
    unit._tokens = unit._preprocessor->run(path);
    unit._scriptFile = unit._sources->load(path);
    unit._diagnostics = unit._preprocessor->diagnostics();
    unit._program = parse(unit._tokens, *unit._preprocessor, unit._diagnostics);
    return unit;
}

SourceLocation ScriptUnit::writtenAt(TokenRange range) const {
    return range.begin < _tokens.size() ? _preprocessor->writtenAt(_tokens[range.begin]) : SourceLocation{};
}

std::vector<const Macro*> ScriptUnit::macroChain(TokenRange range) const {
    return range.begin < _tokens.size() ? _preprocessor->macroChain(_tokens[range.begin]) : std::vector<const Macro*>{};
}

std::optional<int64_t> ScriptUnit::evaluate(const Expr& expr) const {
    if (expr.range.begin >= expr.range.end || expr.range.end > _tokens.size()) {
        return std::nullopt;
    }
    const std::vector<Token> slice(_tokens.begin() + static_cast<std::ptrdiff_t>(expr.range.begin),
        _tokens.begin() + static_cast<std::ptrdiff_t>(expr.range.end));
    return _preprocessor->evaluate(slice);
}

std::optional<int64_t> ScriptUnit::evaluate(const Macro& macro) const {
    return _preprocessor->evaluate(macro);
}

} // namespace geck::ssl
