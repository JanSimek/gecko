#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "resource/DataFileSystem.h"

#ifndef GECK_TEST_DATA_DIR
#define GECK_TEST_DATA_DIR "data"
#endif

// Characterisation tests for the Fallout 2 DAT2 reading stack
// (DataFileSystem -> GeckDat2FileSystem -> Dat2File -> vfspp). These lock the
// observable behaviour (entry count and byte-for-byte decompression) so the
// vfspp API migration can be proven not to change anything.
//
// Fixture: tests/data/f2_res.dat (the Fallout2-CE hi-res resource archive),
// a valid DAT2 with 178 zlib-compressed entries.

using geck::resource::DataFileSystem;

namespace {

std::filesystem::path fixturePath() {
    return std::filesystem::path(GECK_TEST_DATA_DIR) / "f2_res.dat";
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// DAT2 stores backslash, upper-case names; the VFS layer normalises slashes and
// may add an alias prefix. Match on the trailing path so the test is robust to
// that formatting while still pinning the decompressed bytes.
std::optional<std::filesystem::path> findBySuffix(DataFileSystem& dfs, const std::string& lowerSuffix) {
    for (const auto& f : dfs.list("*")) {
        const std::string s = toLower(f.generic_string());
        if (s.size() >= lowerSuffix.size()
            && s.compare(s.size() - lowerSuffix.size(), lowerSuffix.size(), lowerSuffix) == 0) {
            return f;
        }
    }
    return std::nullopt;
}

uint64_t byteSum(const std::vector<uint8_t>& data) {
    uint64_t sum = 0;
    for (uint8_t b : data) {
        sum += b;
    }
    return sum;
}

} // namespace

TEST_CASE("DAT2 archive lists every entry", "[dat][vfs]") {
    REQUIRE(std::filesystem::exists(fixturePath()));

    DataFileSystem dfs;
    dfs.addDataPath(fixturePath());

    REQUIRE(dfs.list("*").size() == 178);
}

TEST_CASE("DAT2 archive decompresses a small entry byte-for-byte", "[dat][vfs]") {
    DataFileSystem dfs;
    dfs.addDataPath(fixturePath());

    const auto path = findBySuffix(dfs, "artemple.edg");
    REQUIRE(path.has_value());

    const auto data = dfs.readRawBytes(*path);
    REQUIRE(data.has_value());

    static const std::vector<uint8_t> kExpected = {
        0x45,
        0x44,
        0x47,
        0x45,
        0x00,
        0x00,
        0x00,
        0x01,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x20,
        0x7A,
        0x00,
        0x00,
        0x21,
        0x1E,
        0x00,
        0x00,
        0x7C,
        0x95,
        0x00,
        0x00,
        0x65,
        0x6E,
        0x00,
        0x00,
        0x00,
        0x01,
        0x00,
        0x00,
        0x00,
        0xC7,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x9B,
        0x78,
        0x00,
        0x00,
        0x9C,
        0x3F,
        0x00,
        0x00,
        0x00,
        0x02,
        0x00,
        0x00,
        0x00,
        0xC7,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x9B,
        0x78,
        0x00,
        0x00,
        0x9C,
        0x3F,
    };

    REQUIRE(data->size() == 68);
    REQUIRE(*data == kExpected);

    REQUIRE(dfs.exists(*path));
}

TEST_CASE("DAT2 archive decompresses a large entry", "[dat][vfs]") {
    DataFileSystem dfs;
    dfs.addDataPath(fixturePath());

    const auto path = findBySuffix(dfs, "hr_alltlk.frm");
    REQUIRE(path.has_value());

    const auto data = dfs.readRawBytes(*path);
    REQUIRE(data.has_value());
    REQUIRE(data->size() == 307274);
    REQUIRE(byteSum(*data) == 29414514u);
}

TEST_CASE("DAT2 archive reports a missing file as absent", "[dat][vfs]") {
    DataFileSystem dfs;
    dfs.addDataPath(fixturePath());

    REQUIRE_FALSE(dfs.exists("does/not/exist.xyz"));
    REQUIRE_FALSE(dfs.readRawBytes("does/not/exist.xyz").has_value());
}
