#include "MsgReader.h"

#include "reader/ErrorMessages.h"
#include "reader/TextParsing.h"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace geck {

namespace {

    using geck::text::splitLines;
    using geck::text::trim;

    // Parse `{id}{audio}{text}` by reading the three brace groups (the engine ends `text` at the first
    // '}' after the third '{', so a message text never contains '}'). Anything after the third '}' is a
    // trailing inline comment. Returns true on a well-formed message line; a non-numeric id is rejected
    // (not a message) — the engine validates the id the same way (message.cc _message_parse_number).
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
        std::erase(line.text, '\n');             // a text that runs over several lines: the engine drops the breaks
        line.inlineComment = raw.substr(c2 + 1); // the trailing "  # ..." (incl. leading ws), or ""
        return true;
    }

    // The engine reads a field up to its closing '}' whatever lines it spans (message.cc
    // _message_load_field), so a message whose text runs over several lines is one message. When the
    // message starting at `first` is not complete on its line, join the following lines until it is
    // and parse that into `line` (raw keeps the line breaks, so it round-trips). Returns the last line
    // used, or `first` when no later line completes it.
    std::size_t continuation(const std::vector<std::string>& rawLines, std::size_t first, MsgLine& line) {
        if (trim(rawLines[first]).front() != '{') {
            return first;
        }
        std::string joined = rawLines[first];
        for (std::size_t i = first + 1; i < rawLines.size(); ++i) {
            joined += '\n';
            joined += rawLines[i];
            if (rawLines[i].find('}') == std::string::npos) {
                continue;
            }
            MsgLine candidate;
            if (parseMessage(joined, candidate)) {
                candidate.raw = std::move(joined);
                line = std::move(candidate);
                return i;
            }
        }
        return first;
    }

} // namespace

Msg parseMsg(const std::filesystem::path& path, const std::string& content) {
    bool finalNewline = true;
    const std::vector<std::string> rawLines = splitLines(content, finalNewline);

    std::vector<MsgLine> lines;
    lines.reserve(rawLines.size());
    for (std::size_t i = 0; i < rawLines.size(); ++i) {
        MsgLine line;
        line.raw = rawLines[i];
        const std::string trimmed = trim(rawLines[i]);
        if (trimmed.empty()) {
            line.kind = MsgLine::Kind::Blank;
        } else if (trimmed.front() == '{' && parseMessage(line.raw, line)) {
            line.kind = MsgLine::Kind::Message;
        } else if (const std::size_t last = continuation(rawLines, i, line); last != i) {
            line.kind = MsgLine::Kind::Message;
            i = last;
        } else {
            line.kind = MsgLine::Kind::Comment; // '#' comment or any non-message line, kept verbatim
        }
        lines.push_back(std::move(line));
    }
    return Msg(path, std::move(lines), finalNewline);
}

std::unique_ptr<Msg> MsgReader::read() {
    try {
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading MSG file: {}", _path.string());

        const auto pos = utils.getPosition();
        if (pos.total == 0) {
            throw UnsupportedFormatException("Empty MSG file", _path);
        }

        FormatValidator::validateMsgFile(utils, _path);

        const std::string content = utils.readFixedString(pos.total);
        auto msg = std::make_unique<Msg>(parseMsg(_path, content));
        spdlog::debug("Read MSG file {}: {} messages", _path.filename().string(), msg->getMessages().size());
        return msg;

    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException("Failed to parse MSG file: " + std::string(e.what()), _path);
    }
}

} // namespace geck
