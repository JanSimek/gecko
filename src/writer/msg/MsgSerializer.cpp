#include "writer/msg/MsgSerializer.h"

#include "reader/TextParsing.h"

#include <string>
#include <vector>

namespace geck::writer {

std::string serializeMsg(const Msg& msg) {
    std::vector<std::string> raws;
    raws.reserve(msg.lines().size());
    for (const MsgLine& line : msg.lines()) {
        raws.push_back(line.raw);
    }
    return geck::text::joinLinesLf(raws, msg.finalNewline());
}

} // namespace geck::writer
