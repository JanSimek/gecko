#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace geck::text {

/// Trim leading and trailing ASCII whitespace (space, tab, CR, LF). Returns "" for all-whitespace.
inline std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

/// ASCII-lowercase a copy of the string.
inline std::string toLower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Drop an inline `;` comment (and everything after it) — Fallout's .txt configs comment any line,
/// including section headers, so strip before parsing.
inline std::string stripComment(const std::string& line) {
    return line.substr(0, line.find(';'));
}

/// Split on commas, trimming each field. Used for the comma-separated value lists (world_pos,
/// entrance rows, terrain lists, encounter entries) in the .txt configs.
inline std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream stream(s);
    std::string item;
    while (std::getline(stream, item, ',')) {
        out.push_back(trim(item));
    }
    return out;
}

/// std::stoi with a fallback; stops at the first non-digit, so "35%" -> 35 and "8{Wielded}" -> 8.
inline int intOr(const std::string& s, int fallback) {
    try {
        return std::stoi(s);
    } catch (const std::exception&) {
        return fallback;
    }
}

} // namespace geck::text
