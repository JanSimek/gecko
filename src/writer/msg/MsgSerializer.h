#pragma once

#include "format/msg/MsgDocument.h"

#include <optional>
#include <string>

namespace geck::writer {

/// Serialise a @ref MsgDocument back to bytes: each line's raw content joined with `\n` (plus a
/// trailing `\n` iff `finalNewline`), so an unedited document is reproduced exactly (modulo CRLF->LF).
std::string serializeMsg(const MsgDocument& doc);

/// The text of message `id`, or nullopt if absent.
std::optional<std::string> findMessageText(const MsgDocument& doc, int id);

/// Set message `id`'s text, keeping the existing audio field + inline comment when the message is
/// present, or appending a new `{id}{}{text}` line when it isn't (upsert). Rewrites only that line.
void setMessageText(MsgDocument& doc, int id, const std::string& text);

} // namespace geck::writer
