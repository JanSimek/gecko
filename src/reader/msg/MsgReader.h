#pragma once

#include "reader/FileParser.h"
#include "format/msg/Msg.h"

#include <filesystem>
#include <string>

namespace geck {

/// Parse `.msg` content into a Msg, line by line: blank / `#` comment / `{id}{audio}{text}` message
/// (with an optional trailing inline comment). Lossless (keeps every line) and builds the id->text
/// lookup. A line whose `{id}` is not a number is not treated as a message — the engine validates the
/// id too. Used both by MsgReader (repository load) and directly when editing a .msg round-trip.
Msg parseMsg(const std::filesystem::path& path, const std::string& content);

class MsgReader : public FileParser<Msg> {
public:
    std::unique_ptr<Msg> read() override;
};

} // namespace geck
