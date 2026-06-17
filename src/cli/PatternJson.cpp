#include "cli/PatternJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

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

namespace {
    pattern::PatternObject parseObject(const nlohmann::json& json) {
        return pattern::PatternObject{
            json.value("dxHex", 0), json.value("dyHex", 0),
            json.value("proPid", 0U), json.value("frmPid", 0U),
            json.value("direction", 0U), json.value("flags", 0U)
        };
    }

    pattern::PatternTile parseTile(const nlohmann::json& json) {
        return pattern::PatternTile{
            json.value("dxTile", 0), json.value("dyTile", 0),
            static_cast<uint16_t>(json.value("tileId", 0U))
        };
    }

    template <typename T, typename Parse>
    std::vector<T> parseArray(const nlohmann::json& parent, const char* key, Parse parse) {
        std::vector<T> out;
        if (const auto it = parent.find(key); it != parent.end() && it->is_array()) {
            out.reserve(it->size());
            std::transform(it->begin(), it->end(), std::back_inserter(out), parse);
        }
        return out;
    }

    pattern::PatternVariant parseVariant(const nlohmann::json& json) {
        pattern::PatternVariant variant;
        variant.label = json.value("label", std::string("default"));
        variant.anchorHex = json.value("anchorHex", 0);
        variant.objects = parseArray<pattern::PatternObject>(json, "objects", parseObject);
        variant.floor = parseArray<pattern::PatternTile>(json, "floor", parseTile);
        variant.roof = parseArray<pattern::PatternTile>(json, "roof", parseTile);
        return variant;
    }
} // namespace

std::optional<pattern::Pattern> loadPattern(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open stamp file: " + path;
        }
        return std::nullopt;
    }
    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = std::string("invalid stamp JSON: ") + e.what();
        }
        return std::nullopt;
    }
    if (!root.is_object() || !root.contains("variants") || !root["variants"].is_array()) {
        if (error != nullptr) {
            *error = "stamp JSON has no 'variants' array";
        }
        return std::nullopt;
    }

    pattern::Pattern pattern;
    pattern.name = root.value("name", std::string());
    pattern.version = root.value("version", pattern::Pattern::CURRENT_VERSION);
    for (const auto& variant : root["variants"]) {
        pattern.variants.push_back(parseVariant(variant));
    }
    return pattern;
}

} // namespace geck::cli
