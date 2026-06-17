#include "cli/PatternJson.h"

#include <format>
#include <sstream>
#include <string>

namespace geck::cli {

namespace {
    // Minimal JSON string literal (quotes + escapes); pattern names are author-supplied.
    std::string jsonString(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('"');
        for (const char c : s) {
            switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                    } else {
                        out.push_back(c);
                    }
            }
        }
        out.push_back('"');
        return out;
    }

    void writeObjects(std::ostream& out, const std::vector<pattern::PatternObject>& objects) {
        bool first = true;
        for (const auto& o : objects) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"dxHex\":" << o.dxHex << ",\"dyHex\":" << o.dyHex
                << ",\"proPid\":" << o.proPid << ",\"frmPid\":" << o.frmPid
                << ",\"direction\":" << o.direction << ",\"flags\":" << o.flags << "}";
        }
    }

    void writeTiles(std::ostream& out, const std::vector<pattern::PatternTile>& tiles) {
        bool first = true;
        for (const auto& t : tiles) {
            out << (first ? "" : ",");
            first = false;
            out << "{\"dxTile\":" << t.dxTile << ",\"dyTile\":" << t.dyTile << ",\"tileId\":" << t.tileId << "}";
        }
    }
} // namespace

std::string serializePattern(const pattern::Pattern& pattern) {
    std::ostringstream out;
    out << "{\"name\":" << jsonString(pattern.name) << ",\"version\":" << pattern.version << ",\"variants\":[";
    bool firstVariant = true;
    for (const auto& variant : pattern.variants) {
        out << (firstVariant ? "" : ",");
        firstVariant = false;
        out << "{\"label\":" << jsonString(variant.label) << ",\"anchorHex\":" << variant.anchorHex
            << ",\"objects\":[";
        writeObjects(out, variant.objects);
        out << "],\"floor\":[";
        writeTiles(out, variant.floor);
        out << "],\"roof\":[";
        writeTiles(out, variant.roof);
        out << "]}";
    }
    out << "]}\n";
    return out.str();
}

} // namespace geck::cli
