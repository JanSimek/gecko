#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "resource/DataFileSystem.h"

namespace geck::test {

/// The listed path ending in `suffix`, compared case-insensitively. DAT2 stores backslash,
/// upper-case names; the VFS layer normalises slashes and may add an alias prefix, so a suffix
/// match is robust to that formatting while still naming one entry.
inline std::optional<std::filesystem::path> findBySuffix(resource::DataFileSystem& files, std::string_view suffix) {
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string wanted = lower(std::string(suffix));
    for (const auto& file : files.list("*")) {
        if (lower(file.generic_string()).ends_with(wanted)) {
            return file;
        }
    }
    return std::nullopt;
}

/// The sum of every byte: a cheap content fingerprint for pinning decompressed archive entries.
inline uint64_t byteSum(const std::vector<uint8_t>& data) {
    return std::accumulate(data.begin(), data.end(), uint64_t{ 0 });
}

} // namespace geck::test
