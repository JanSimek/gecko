#include "writer/gam/GamSerializer.h"

#include "reader/TextParsing.h"

#include <string>
#include <vector>

namespace geck::writer {

std::string serializeGam(const Gam& doc) {
    std::vector<std::string> raws;
    raws.reserve(doc.lines.size());
    for (const GamLine& line : doc.lines) {
        raws.push_back(line.raw);
    }
    return geck::text::joinLinesLf(raws, doc.finalNewline);
}

} // namespace geck::writer
