#pragma once

#include <string>
#include <vector>

/// @file
/// @brief Lossless, round-trippable document model for a Fallout 2 `.msg` file (e.g. `map.msg`).
///
/// The lossy @ref Msg reader keeps only id->text and drops comments, blank lines, the audio field's
/// formatting, and inline comments. This model preserves the whole file — every line in order, as a
/// `#` comment / blank / `{id}{audio}{text}` message (with any trailing `# ...` inline comment) —
/// keeping each line's raw content as canonical so an unchanged line round-trips and editing one
/// message rewrites only that line. Line endings are normalised to LF on write (the engine's message
/// parser skips whitespace between `{}` fields, so it reads either — see fallout2-ce message.cc).

namespace geck {

struct MsgLine {
    enum class Kind {
        Blank,
        Comment, ///< a `#`-prefixed (or otherwise non-message) line
        Message, ///< `{id}{audio}{text}` with an optional trailing `# ...` inline comment
    };

    Kind kind = Kind::Blank;
    std::string raw;           ///< the line content without its EOL — canonical for serialisation
    int id = -1;               ///< Message: the message id
    std::string audio;         ///< Message: the audio field (often empty)
    std::string text;          ///< Message: the message text
    std::string inlineComment; ///< Message: the `  # ...` suffix incl. its leading whitespace, or ""
};

struct MsgDocument {
    std::vector<MsgLine> lines;
    bool finalNewline = true;

    const MsgLine* message(int id) const {
        for (const auto& line : lines) {
            if (line.kind == MsgLine::Kind::Message && line.id == id) {
                return &line;
            }
        }
        return nullptr;
    }

    MsgLine* message(int id) {
        for (auto& line : lines) {
            if (line.kind == MsgLine::Kind::Message && line.id == id) {
                return &line;
            }
        }
        return nullptr;
    }
};

} // namespace geck
