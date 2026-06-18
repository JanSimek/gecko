#include "cli/PatternJson.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geck::cli {

namespace {
    using nlohmann::ordered_json; // ordered, so the JSON keeps this field order for humans/diffs

    ordered_json objectToJson(const pattern::PatternObject& o) {
        ordered_json j;
        j["dxHex"] = o.dxHex;
        j["dyHex"] = o.dyHex;
        j["proPid"] = o.proPid;
        j["frmPid"] = o.frmPid;
        j["direction"] = o.direction;
        j["flags"] = o.flags;
        return j;
    }

    ordered_json tileToJson(const pattern::PatternTile& t) {
        ordered_json j;
        j["dxTile"] = t.dxTile;
        j["dyTile"] = t.dyTile;
        j["tileId"] = t.tileId;
        return j;
    }

    template <typename T, typename ToJson>
    ordered_json arrayOf(const std::vector<T>& items, ToJson toJson) {
        auto array = ordered_json::array();
        for (const auto& item : items) {
            array.push_back(toJson(item));
        }
        return array;
    }
} // namespace

std::string serializePattern(const pattern::Pattern& pattern) {
    ordered_json root;
    root["name"] = pattern.name;
    root["version"] = pattern.version;
    auto variants = ordered_json::array();
    for (const auto& variant : pattern.variants) {
        ordered_json entry;
        entry["label"] = variant.label;
        entry["anchorHex"] = variant.anchorHex;
        entry["objects"] = arrayOf(variant.objects, objectToJson);
        entry["floor"] = arrayOf(variant.floor, tileToJson);
        entry["roof"] = arrayOf(variant.roof, tileToJson);
        variants.push_back(std::move(entry));
    }
    root["variants"] = std::move(variants);
    return root.dump() + "\n"; // compact, matching the editor's reader
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
