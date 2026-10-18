#pragma once

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// GECK_TEST_TMP_DIR is injected per test target (a directory inside the build
// tree). Writing scratch files there instead of the shared, world-writable
// system temp directory keeps test artifacts contained and avoids using a
// predictable name in a public directory.
#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

namespace geck::test {

namespace detail {

    /// "-<pid>": ctest runs every test case in its own process, possibly in parallel, so scratch
    /// names carry the process id to keep concurrent cases from sharing (and deleting) a path.
    inline std::string processSuffix() {
#ifdef _WIN32
        return "-" + std::to_string(_getpid());
#else
        return "-" + std::to_string(getpid());
#endif
    }

    /// A process-wide sequence number, so two scratch directories of one name never collide.
    inline unsigned nextScratchId() {
        static std::atomic<unsigned> counter{ 0 };
        return ++counter;
    }

} // namespace detail

/// RAII scratch file under the build-tree temp directory, named `<stem>-<pid><extension>`. Removes
/// any stale file of the same name on construction and unlinks on destruction. Files of one stem
/// share the suffix, so a map and its sibling (`x.map` / `x.EDG`) still pair by stem.
class TempFile {
public:
    TempFile(const std::string& stem, const std::string& extension) {
        const std::filesystem::path dir{ GECK_TEST_TMP_DIR };
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        _path = dir / (stem + detail::processSuffix() + extension);
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

/// RAII scratch *directory* under the same build-tree temp root, named `<name>-<pid>-<n>` so no two
/// instances — in one process or across concurrent ones — share a tree. Created empty on
/// construction and removed with its contents on destruction.
///
/// The join with GECK_TEST_TMP_DIR lives here rather than in each test because cpp:S5443 flags
/// every use as a publicly-writable-directory hotspot: it cannot see through the macro to know
/// this is inside the build tree, so the construct is worth keeping in one reviewed place.
class TempDir {
public:
    explicit TempDir(const std::string& name) {
        _path = std::filesystem::path{ GECK_TEST_TMP_DIR }
            / (name + detail::processSuffix() + "-" + std::to_string(detail::nextScratchId()));
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

/// Writes `contents` to `path` verbatim (binary, so "\n" stays LF on Windows), creating the parent
/// directories; fails the test if the file cannot be written.
inline void writeFile(const std::filesystem::path& path, std::string_view contents) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    REQUIRE_FALSE(out.fail());
}

/// Writes `bytes` to `path`, creating the parent directories; fails the test on a write error.
inline void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    writeFile(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

} // namespace geck::test
