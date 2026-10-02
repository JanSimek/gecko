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

/// RAII scratch *directory* under the same build-tree temp root, for tests that mount a data path
/// rather than read one file. Removes any stale tree of the same name on construction and on
/// destruction, so a failed run cannot leak state into the next one.
///
/// The join with GECK_TEST_TMP_DIR lives here rather than in each test because cpp:S5443 flags
/// every use as a publicly-writable-directory hotspot: it cannot see through the macro to know
/// this is inside the build tree, so the construct is worth keeping in one reviewed place.
class TempDir {
public:
    explicit TempDir(const std::string& name) {
        _path = std::filesystem::path{ GECK_TEST_TMP_DIR } / name;
        remove();
        std::error_code ec;
        std::filesystem::create_directories(_path, ec);
    }

    ~TempDir() { remove(); }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return _path; }
    std::string string() const { return _path.string(); }

private:
    void remove() const {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
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
