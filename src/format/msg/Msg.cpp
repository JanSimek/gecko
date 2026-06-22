#include "Msg.h"

#include <utility>

namespace geck {

namespace {
    std::string messageRaw(int id, const std::string& audio, const std::string& text, const std::string& inlineComment) {
        return "{" + std::to_string(id) + "}{" + audio + "}{" + text + "}" + inlineComment;
    }
} // namespace

Msg::Msg(std::filesystem::path path, std::vector<MsgLine> lines, bool finalNewline)
    : IFile(std::move(path))
    , _lines(std::move(lines))
    , _finalNewline(finalNewline) {
    // Build the id->message lookup from the message lines. A duplicate id keeps the last entry, which
    // is what the engine does (message.cc updates the existing entry).
    for (const MsgLine& line : _lines) {
        if (line.kind == MsgLine::Kind::Message) {
            _messages[line.id] = { line.id, line.audio, line.text };
        }
    }
}

Msg::Msg(std::filesystem::path path, std::map<int, Message> messages)
    : IFile(std::move(path))
    , _finalNewline(true)
    , _messages(std::move(messages)) {
    for (const auto& [id, message] : _messages) {
        MsgLine line;
        line.kind = MsgLine::Kind::Message;
        line.id = id;
        line.audio = message.audio;
        line.text = message.text;
        line.raw = messageRaw(id, message.audio, message.text, "");
        _lines.push_back(std::move(line));
    }
}

const Msg::Message& Msg::message(int id) {
    return _messages[id];
}

const std::map<int, Msg::Message>& Msg::getMessages() const {
    return _messages;
}

void Msg::setMessageText(int id, const std::string& text) {
    for (MsgLine& line : _lines) {
        if (line.kind == MsgLine::Kind::Message && line.id == id) {
            line.text = text;
            line.raw = messageRaw(id, line.audio, text, line.inlineComment); // keep audio + inline comment
            _messages[id] = { id, line.audio, text };
            return;
        }
    }
    MsgLine line;
    line.kind = MsgLine::Kind::Message;
    line.id = id;
    line.text = text;
    line.raw = messageRaw(id, "", text, "");
    _lines.push_back(line);
    _messages[id] = { id, "", text };
}

} // namespace geck
