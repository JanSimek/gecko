#pragma once

#include <algorithm>
#include <cctype>
#include <string>

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

} // namespace geck::text
