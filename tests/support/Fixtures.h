#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// GECK_TEST_DATA_DIR is injected per test target via target_compile_definitions
// and points at the in-source tests/data tree. Resolving fixtures by absolute
// path makes tests independent of the working directory (running the binary
// directly, from a debugger, or an IDE no longer silently fails to find data).
#ifndef GECK_TEST_DATA_DIR
#error "GECK_TEST_DATA_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif
// GECK_SCRIPTS_DIR is the in-source scripts/ tree the build bundles into the editor.
#ifndef GECK_SCRIPTS_DIR
#error "GECK_SCRIPTS_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

namespace geck::test {

/// Absolute path to the in-source tests/data directory.
inline std::filesystem::path dataDir() {
    return std::filesystem::path(GECK_TEST_DATA_DIR);
}

/// Absolute path to a fixture file under tests/data.
inline std::filesystem::path dataPath(const std::string& name) {
    return dataDir() / name;
}

/// The text of a bundled script, `relative` to the in-source scripts/ tree (e.g. "fills/desert.luau").
inline std::string bundledScript(const std::string& relative) {
    std::ifstream file(std::filesystem::path(GECK_SCRIPTS_DIR) / relative, std::ios::binary);
    REQUIRE(file.is_open());
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

/// tests/data/f2_res.dat, the Fallout2-CE hi-res resource archive: a valid DAT2 of zlib-compressed
/// entries, the committed DAT fixture.
inline std::filesystem::path f2ResDat() {
    return dataPath("f2_res.dat");
}

/// The number of entries in f2_res.dat.
inline constexpr size_t F2_RES_ENTRY_COUNT = 178;
/// A large, compressed entry of f2_res.dat, and its decompressed size.
inline constexpr std::string_view HR_ALLTLK = "art/intrface/hr_alltlk.frm";
inline constexpr size_t HR_ALLTLK_SIZE = 307274;

/// `text` with every LF turned into CRLF, the line ending of the shipped DOS-era text files.
inline std::string toCrlf(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (const char c : text) {
        if (c == '\n') {
            out += '\r';
        }
        out += c;
    }
    return out;
}

/// The bytes of `text`, for readers that parse a byte buffer.
inline std::vector<uint8_t> bytesOf(std::string_view text) {
    return { text.begin(), text.end() };
}

} // namespace geck::test
