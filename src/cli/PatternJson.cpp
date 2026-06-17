#include "cli/PatternJson.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
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
    // Validated reads, mirroring the editor's PatternSerializer: a stamp the MCP/scripts feed to
    // generate must have its required fields present, integer and in range — a bad stamp fails as an
    // error rather than silently becoming PID-0 objects or wrapped tile ids.
    constexpr std::int64_t kI32Lo = INT32_MIN;
    constexpr std::int64_t kI32Hi = INT32_MAX;
    constexpr std::int64_t kU32Hi = 0xFFFFFFFFLL;
    constexpr std::int64_t kU16Hi = 0xFFFFLL;

    bool readInt(const nlohmann::json& obj, const char* key, std::int64_t lo, std::int64_t hi,
        std::int64_t& out, std::string* error) {
        const auto it = obj.find(key);
        if (it == obj.end() || !it->is_number_integer()) {
            if (error != nullptr) {
                *error = std::string("missing or non-integer field '") + key + "'";
            }
            return false;
        }
        const std::int64_t value = it->get<std::int64_t>();
        if (value < lo || value > hi) {
            if (error != nullptr) {
                *error = std::string("field '") + key + "' is out of range";
            }
            return false;
        }
        out = value;
        return true;
    }

    // Optional int: absent -> default; present -> must be valid.
    bool readOptInt(const nlohmann::json& obj, const char* key, std::int64_t lo, std::int64_t hi,
        std::int64_t dflt, std::int64_t& out, std::string* error) {
        if (obj.find(key) == obj.end()) {
            out = dflt;
            return true;
        }
        return readInt(obj, key, lo, hi, out, error);
    }

    bool parseObject(const nlohmann::json& json, pattern::PatternObject& out, std::string* error) {
        if (!json.is_object()) {
            if (error != nullptr) {
                *error = "object entry is not a JSON object";
            }
            return false;
        }
        std::int64_t dx = 0, dy = 0, pro = 0, frm = 0, dir = 0, flags = 0;
        if (!readInt(json, "dxHex", kI32Lo, kI32Hi, dx, error) || !readInt(json, "dyHex", kI32Lo, kI32Hi, dy, error)
            || !readInt(json, "proPid", 0, kU32Hi, pro, error) || !readInt(json, "frmPid", 0, kU32Hi, frm, error)
            || !readOptInt(json, "direction", 0, kU32Hi, 0, dir, error)
            || !readOptInt(json, "flags", 0, kU32Hi, 0, flags, error)) {
            return false;
        }
        out = { static_cast<int>(dx), static_cast<int>(dy), static_cast<uint32_t>(pro),
            static_cast<uint32_t>(frm), static_cast<uint32_t>(dir), static_cast<uint32_t>(flags) };
        return true;
    }

    bool parseTile(const nlohmann::json& json, pattern::PatternTile& out, std::string* error) {
        if (!json.is_object()) {
            if (error != nullptr) {
                *error = "tile entry is not a JSON object";
            }
            return false;
        }
        std::int64_t dx = 0, dy = 0, id = 0;
        if (!readInt(json, "dxTile", kI32Lo, kI32Hi, dx, error) || !readInt(json, "dyTile", kI32Lo, kI32Hi, dy, error)
            || !readInt(json, "tileId", 0, kU16Hi, id, error)) {
            return false;
        }
        out = { static_cast<int>(dx), static_cast<int>(dy), static_cast<uint16_t>(id) };
        return true;
    }

    template <typename T, typename Parse>
    bool parseArray(const nlohmann::json& parent, const char* key, std::vector<T>& out, Parse parse, std::string* error) {
        const auto it = parent.find(key);
        if (it == parent.end()) {
            return true; // absent array -> empty, as the editor's reader allows
        }
        if (!it->is_array()) {
            if (error != nullptr) {
                *error = std::string("field '") + key + "' is not an array";
            }
            return false;
        }
        out.reserve(it->size());
        for (const auto& entry : *it) {
            T item;
            if (!parse(entry, item, error)) {
                return false;
            }
            out.push_back(item);
        }
        return true;
    }

    bool parseVariant(const nlohmann::json& json, pattern::PatternVariant& out, std::string* error) {
        if (!json.is_object()) {
            if (error != nullptr) {
                *error = "variant entry is not a JSON object";
            }
            return false;
        }
        std::int64_t anchor = 0;
        if (!readInt(json, "anchorHex", kI32Lo, kI32Hi, anchor, error)) {
            return false;
        }
        out.anchorHex = static_cast<int>(anchor);
        out.label = json.value("label", std::string("default"));
        return parseArray(json, "objects", out.objects, parseObject, error)
            && parseArray(json, "floor", out.floor, parseTile, error)
            && parseArray(json, "roof", out.roof, parseTile, error);
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
        pattern::PatternVariant parsed;
        if (!parseVariant(variant, parsed, error)) {
            return std::nullopt;
        }
        pattern.variants.push_back(std::move(parsed));
    }
    return pattern;
}

} // namespace geck::cli
