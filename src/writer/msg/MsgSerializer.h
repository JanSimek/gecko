#pragma once

#include "format/msg/Msg.h"

#include <string>

namespace geck::writer {

/// Serialise a @ref Msg back to bytes: each line's raw content joined with `\n` (plus a trailing `\n`
/// iff `finalNewline`), so an unedited file is reproduced exactly (modulo CRLF->LF). Editing a message
/// is done via Msg::setMessageText, which rewrites only that line.
std::string serializeMsg(const Msg& msg);

} // namespace geck::writer
