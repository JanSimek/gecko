#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ssl/CallGraph.h"
#include "ssl/ScriptUnit.h"

namespace geck::ssl {

/// One call of an opcode in code the compiled script runs, with what is known about its arguments.
struct CallRef {
    std::string opcode;                       ///< lowercased
    std::vector<std::optional<int64_t>> args; ///< each argument's value, when it folds to a constant
    std::vector<bool> self;                   ///< each argument is `self_obj`
    std::string procedure;
    SourceLocation location; ///< where the user wrote it
};

/// Every call of one of `opcodes` (case-insensitive) in the live procedures of `unit`, in source order.
std::vector<CallRef> callRefs(const ScriptUnit& unit, const CallGraph& graph, std::initializer_list<std::string_view> opcodes);

} // namespace geck::ssl
