#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// GECK_TEST_TMP_DIR is injected per test target (a directory inside the build
// tree). Writing scratch files there instead of the shared, world-writable
// system temp directory keeps test artifacts contained and avoids using a
// predictable name in a public directory.
#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

namespace geck::test {

/// RAII scratch file under the build-tree temp directory. Removes any stale file
/// of the same name on construction and unlinks on destruction, replacing the
/// hand-written temp-path + `std::error_code; remove(...)` scaffolding repeated
/// across the round-trip suites.
class TempFile {
public:
    TempFile(const std::string& stem, const std::string& extension) {
        const std::filesystem::path dir{ GECK_TEST_TMP_DIR };
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        _path = dir / (stem + extension);
        remove();
    }

    ~TempFile() { remove(); }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const std::filesystem::path& path() const { return _path; }

private:
    void remove() const {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }

    std::filesystem::path _path;
};

/// Reads an entire file into a byte vector for byte-for-byte comparisons.
inline std::vector<uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream stream{ path, std::ios::binary };
    REQUIRE(stream.is_open());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

} // namespace geck::test
