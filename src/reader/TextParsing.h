#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
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

/// Parse a numeric field the way the engine does — std::atoi: take the leading digits, yield 0 for a
/// non-numeric value, never error (so "35%" -> 35 and "8{Wielded}" -> 8). An empty (absent) field keeps
/// `fallback`, which callers set to the field's default — matching the engine's struct default for an
/// absent field. (A present-but-non-numeric value therefore yields 0, like the engine, not the default.)
inline int intOr(const std::string& s, int fallback) {
    return s.empty() ? fallback : std::atoi(s.c_str());
}

/// Split into per-line content with the EOL removed and a trailing '\r' stripped, so a CRLF or LF file
/// yields the same lines. `finalNewline` is set to whether the content ended with a newline. Rejoining
/// via joinLinesLf reproduces the input modulo CRLF->LF normalisation. Used by the round-trip document
/// readers (maps.txt, map.msg).
inline std::vector<std::string> splitLines(const std::string& content, bool& finalNewline) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < content.size()) {
        const std::size_t nl = content.find('\n', start);
        if (nl == std::string::npos) {
            std::string line = content.substr(start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            finalNewline = false;
            return lines;
        }
        std::string line = content.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        start = nl + 1;
    }
    finalNewline = !content.empty();
    return lines;
}

/// Join lines with '\n' (plus a trailing '\n' iff `finalNewline`) — the inverse of splitLines.
inline std::string joinLinesLf(const std::vector<std::string>& lines, bool finalNewline) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || finalNewline) {
            out += '\n';
        }
    }
    return out;
}

} // namespace geck::text
