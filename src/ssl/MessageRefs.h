#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ssl/CallGraph.h"
#include "ssl/ScriptUnit.h"

namespace geck::ssl {

/// One use of a dialogue message by number, in code the compiled script runs: `message_str`,
/// `gsay_reply`, `gsay_message`, `gsay_option` or `giq_option`.
///
/// The engine reads message `number` from list `list` (fallout2-ce scripts.cc scriptsGetMessageList):
/// the list is the 1-based scripts.lst line, i.e. a SCRIPT_* constant, and names that script's
/// text/<language>/dialog/<name>.msg. The script's own list is its `NAME` macro.
struct MessageRef {
    std::string opcode;        ///< lowercased, e.g. "gsay_reply"
    std::optional<int> list;   ///< when the list argument is a constant
    std::optional<int> number; ///< when the message argument is a constant number (not a string)
    std::string procedure;
    SourceLocation location; ///< where the user wrote it
};

/// Every message use in the live procedures of `unit`, in source order.
std::vector<MessageRef> messageRefs(const ScriptUnit& unit, const CallGraph& graph);

/// Whether `list`/`number` is one of the pairs the engine answers without a message file: 0/0 and
/// -1/-1 (empty text) and -2/-2 (proto.msg's 650).
bool isBuiltInMessage(int list, int number);

} // namespace geck::ssl
