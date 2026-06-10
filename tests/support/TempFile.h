#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace geck::test {

/// RAII temp file path under the system temp directory. Removes any stale file
/// of the same name on construction and unlinks on destruction, replacing the
/// hand-written `temp_directory_path() / name` + `std::error_code; remove(...)`
/// scaffolding repeated across the round-trip suites.
class TempFile {
public:
    TempFile(const std::string& stem, const std::string& extension) {
        _path = std::filesystem::temp_directory_path() / (stem + extension);
        remove();
    }

    ~TempFile() { remove(); }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const std::filesystem::path& path() const { return _path; }
    operator const std::filesystem::path&() const { return _path; }

private:
    void remove() {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }

    std::filesystem::path _path;
};

/// Reads an entire file into a byte vector for byte-for-byte comparisons.
inline std::vector<uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream stream{ path.string(), std::ios::binary };
    REQUIRE(stream.is_open());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

} // namespace geck::test
