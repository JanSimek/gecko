#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace geck::io {

/// Read an entire file as raw bytes (binary). Returns "" if the file can't be opened.
inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// Write `content` to `path` (binary, truncating), creating parent directories first.
inline void writeFile(const std::filesystem::path& path, const std::string& content) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

} // namespace geck::io
