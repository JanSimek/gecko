#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "ssl/Ast.h"

namespace geck::ssl {

/// A comparison or logical operator's result as C (and so sslc and its preprocessor) folds it: 1 or 0.
inline std::optional<int64_t> boolean(bool value) {
    return value ? 1 : 0;
}

/// Integer constant folding over the syntax tree, as sslc's optimizer folds before it removes dead
/// branches. Macros are already expanded in the tree, so a name is a variable, an opcode or sslc's
/// `true`/`false`. A variable folds when it is known: a global the script never assigns, which keeps
/// its initial value.
class ConstantFolder {
public:
    /// Learns which of `program`'s globals are constants: local to the script (not imported or
    /// exported, so no other script can change them) and never assigned by any of its procedures.
    explicit ConstantFolder(const Program& program);

    /// The value `expr` folds to, or nullopt. `shadowed` are the names local to the procedure the
    /// expression is in (parameters, local variables), which hide globals of the same name.
    std::optional<int64_t> fold(const Expr& expr, const std::set<std::string>& shadowed = {}) const;

    /// The globals known to be constant, lowercased, with their values.
    const std::map<std::string, int64_t>& constants() const { return _constants; }

private:
    std::map<std::string, int64_t> _constants;
};

} // namespace geck::ssl
