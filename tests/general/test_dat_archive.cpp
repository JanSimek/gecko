#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "resource/DataFileSystem.h"
#include "support/Fixtures.h"
#include "support/VfsLookup.h"

// Characterisation tests for the Fallout 2 DAT2 reading stack
// (DataFileSystem -> GeckDat2FileSystem -> Dat2File -> vfspp): entry count and
// byte-for-byte decompression.
//
// Fixture: tests/data/f2_res.dat (the Fallout2-CE hi-res resource archive),
// a valid DAT2 of zlib-compressed entries.

using geck::resource::DataFileSystem;

using geck::test::byteSum;
using geck::test::findBySuffix;

TEST_CASE("DAT2 archive lists every entry", "[dat][vfs]") {
    REQUIRE(std::filesystem::exists(geck::test::f2ResDat()));

    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

    REQUIRE(dfs.list("*").size() == geck::test::F2_RES_ENTRY_COUNT);
}

TEST_CASE("DAT2 archive decompresses a small entry byte-for-byte", "[dat][vfs]") {
    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

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
    dfs.addDataPath(geck::test::f2ResDat());

    const auto path = findBySuffix(dfs, geck::test::HR_ALLTLK);
    REQUIRE(path.has_value());

    const auto data = dfs.readRawBytes(*path);
    REQUIRE(data.has_value());
    REQUIRE(data->size() == geck::test::HR_ALLTLK_SIZE);
    REQUIRE(byteSum(*data) == 29414514u);
}

TEST_CASE("DAT2 archive reports a missing file as absent", "[dat][vfs]") {
    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

    REQUIRE_FALSE(dfs.exists("does/not/exist.xyz"));
    REQUIRE_FALSE(dfs.readRawBytes("does/not/exist.xyz").has_value());
}
