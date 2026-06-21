#include "writer/msg/MsgSerializer.h"

#include <cstddef>
#include <string>

namespace geck::writer {

namespace {
    std::string messageRaw(int id, const std::string& audio, const std::string& text, const std::string& inlineComment) {
        return "{" + std::to_string(id) + "}{" + audio + "}{" + text + "}" + inlineComment;
    }
} // namespace

std::string serializeMsg(const MsgDocument& doc) {
    std::string out;
    for (std::size_t i = 0; i < doc.lines.size(); ++i) {
        out += doc.lines[i].raw;
        if (i + 1 < doc.lines.size() || doc.finalNewline) {
            out += '\n';
        }
    }
    return out;
}

std::optional<std::string> findMessageText(const MsgDocument& doc, int id) {
    const MsgLine* line = doc.message(id);
    return line != nullptr ? std::optional<std::string>(line->text) : std::nullopt;
}

void setMessageText(MsgDocument& doc, int id, const std::string& text) {
    if (MsgLine* line = doc.message(id); line != nullptr) {
        line->text = text;
        line->raw = messageRaw(id, line->audio, text, line->inlineComment); // keep audio + comment
        return;
    }
    MsgLine line;
    line.kind = MsgLine::Kind::Message;
    line.id = id;
    line.text = text;
    line.raw = messageRaw(id, "", text, "");
    doc.lines.push_back(std::move(line));
}

} // namespace geck::writer
