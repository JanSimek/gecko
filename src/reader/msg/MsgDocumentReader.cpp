#include "reader/msg/MsgDocumentReader.h"

#include "reader/TextParsing.h"

#include <cstddef>
#include <vector>

namespace geck {

namespace {

    using geck::text::splitLines;
    using geck::text::trim;

    // Parse `{id}{audio}{text}` by reading the three brace groups (the engine ends `text` at the first
    // '}' after the third '{', so a message text never contains '}'). Anything after the third '}' is
    // a trailing inline comment. Returns true on a well-formed message line.
    bool parseMessage(const std::string& raw, MsgLine& line) {
        const std::size_t o0 = raw.find('{');
        if (o0 == std::string::npos) {
            return false;
        }
        const std::size_t c0 = raw.find('}', o0);
        const std::size_t o1 = c0 == std::string::npos ? std::string::npos : raw.find('{', c0);
        const std::size_t c1 = o1 == std::string::npos ? std::string::npos : raw.find('}', o1);
        const std::size_t o2 = c1 == std::string::npos ? std::string::npos : raw.find('{', c1);
        const std::size_t c2 = o2 == std::string::npos ? std::string::npos : raw.find('}', o2);
        if (c2 == std::string::npos) {
            return false;
        }
        try {
            line.id = std::stoi(raw.substr(o0 + 1, c0 - o0 - 1));
        } catch (const std::exception&) {
            return false;
        }
        line.audio = raw.substr(o1 + 1, c1 - o1 - 1);
        line.text = raw.substr(o2 + 1, c2 - o2 - 1);
        line.inlineComment = raw.substr(c2 + 1); // the trailing "  # ..." (incl. leading ws), or ""
        return true;
    }

} // namespace

MsgDocument parseMsgDocument(const std::string& content) {
    MsgDocument doc;
    bool finalNewline = true;
    const std::vector<std::string> rawLines = splitLines(content, finalNewline);
    doc.finalNewline = finalNewline;

    for (const std::string& raw : rawLines) {
        MsgLine line;
        line.raw = raw;
        const std::string trimmed = trim(raw);
        if (trimmed.empty()) {
            line.kind = MsgLine::Kind::Blank;
        } else if (trimmed.front() == '{' && parseMessage(raw, line)) {
            line.kind = MsgLine::Kind::Message;
        } else {
            line.kind = MsgLine::Kind::Comment; // '#' comment or any non-message line, kept verbatim
        }
        doc.lines.push_back(std::move(line));
    }
    return doc;
}

} // namespace geck
