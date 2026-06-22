#pragma once

#include "format/msg/MsgDocument.h"

#include <string>

namespace geck {

/// Parse a whole `.msg` file into a lossless @ref MsgDocument: every line in order (blank / `#`
/// comment / `{id}{audio}{text}` message), each keeping its raw content. A trailing `\r` is stripped
/// per line; `finalNewline` records the trailing newline. Never throws.
MsgDocument parseMsgDocument(const std::string& content);

} // namespace geck
