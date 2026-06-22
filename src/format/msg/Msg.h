#pragma once

#include "format/IFile.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace geck {

/// One line of a `.msg` file, preserved for lossless round-trip: a blank line, a `#` comment (or any
/// non-message line), or a `{id}{audio}{text}` message with an optional trailing `# ...` inline comment.
/// `raw` is the line content (sans EOL) and is canonical for serialisation, so an unedited line is
/// reproduced exactly.
struct MsgLine {
    enum class Kind {
        Blank,
        Comment,
        Message,
    };

    Kind kind = Kind::Blank;
    std::string raw;
    int id = -1;
    std::string audio;
    std::string text;
    std::string inlineComment;
};

/// A Fallout 2 `.msg` file. Parsed line-by-line — the way the engine's message parser reads it, field by
/// field (message.cc) — so a single model both serves id->text lookups (message / getMessages) for the
/// read-only consumers AND round-trips losslessly (lines / finalNewline / setMessageText) for editing.
class Msg : public IFile {
public:
    struct Message {
        int id = 0;
        std::string audio;
        std::string text;
    };

    Msg(std::filesystem::path path, std::vector<MsgLine> lines, bool finalNewline = true);

    /// Construct from an id->message map, synthesising the matching `{id}{audio}{text}` lines. For
    /// programmatic/synthetic construction (tests, generated messages).
    Msg(std::filesystem::path path, std::map<int, Message> messages);

    /// The message with `id`, or a default-constructed one if absent (legacy lookup behaviour).
    const Message& message(int id);
    const std::map<int, Message>& getMessages() const;

    /// Lossless round-trip view: every line in order, and whether the file ended with a newline.
    const std::vector<MsgLine>& lines() const { return _lines; }
    bool finalNewline() const { return _finalNewline; }

    /// Set message `id`'s text, keeping its audio + inline comment, or append a new `{id}{}{text}` line
    /// if absent (upsert). Updates both the line (for round-trip) and the lookup index.
    void setMessageText(int id, const std::string& text);

private:
    std::vector<MsgLine> _lines;
    bool _finalNewline = true;
    std::map<int, Message> _messages; // id -> message, derived from the Message lines for fast lookup
};

} // namespace geck
