#include "writer/msg/MsgSerializer.h"

#include "reader/TextParsing.h"

#include <string>
#include <utility>
#include <vector>

namespace geck::writer {

namespace {
    std::string messageRaw(int id, const std::string& audio, const std::string& text, const std::string& inlineComment) {
        return "{" + std::to_string(id) + "}{" + audio + "}{" + text + "}" + inlineComment;
    }
} // namespace

std::string serializeMsg(const MsgDocument& doc) {
    std::vector<std::string> raws;
    raws.reserve(doc.lines.size());
    for (const MsgLine& line : doc.lines) {
        raws.push_back(line.raw);
    }
    return geck::text::joinLinesLf(raws, doc.finalNewline);
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
