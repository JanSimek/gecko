#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ssl/CallGraph.h"
#include "ssl/ScriptUnit.h"

namespace geck::ssl {

/// One read or write of a global variable (`global_var` / `set_global_var`) or a script's local
/// variable (`local_var` / `set_local_var`), in code the compiled script runs.
struct VariableRef {
    enum class Kind {
        Get,
        Set,
    };
    Kind kind = Kind::Get;
    std::optional<int> index; ///< the variable's number, when the argument is a constant
    /// The argument as written: the macro (GVAR_RADSCORPION_SEED, LVAR_Hostile) or, when the macro
    /// is not defined, the bare name. Empty for a literal number.
    std::string name;
    std::string procedure;           ///< the procedure the access is in
    SourceLocation location;         ///< where the user wrote it
    std::vector<std::string> macros; ///< the macros the access came out of, outermost first
};

/// Every global variable access in the live procedures of `unit` (see CallGraph::isLive), in source order.
std::vector<VariableRef> globalVariableRefs(const ScriptUnit& unit, const CallGraph& graph);

/// Every local variable access in the live procedures of `unit`, in source order.
std::vector<VariableRef> localVariableRefs(const ScriptUnit& unit, const CallGraph& graph);

} // namespace geck::ssl
