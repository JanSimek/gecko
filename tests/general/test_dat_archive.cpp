#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

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

TEST_CASE("DAT2 archive lists every entry", "[dat]") {
    REQUIRE(std::filesystem::exists(geck::test::f2ResDat()));

    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

    CHECK(dfs.list("*").size() == geck::test::F2_RES_ENTRY_COUNT);
}

TEST_CASE("DAT2 archive decompresses a small entry byte-for-byte", "[dat]") {
    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

    const auto path = findBySuffix(dfs, "artemple.edg");
    REQUIRE(path.has_value());

    const auto data = dfs.readRawBytes(*path);
    REQUIRE(data.has_value());

    // clang-format off
    static const std::vector<uint8_t> kExpected = {
        0x45, 0x44, 0x47, 0x45, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x7A,
        0x00, 0x00, 0x21, 0x1E, 0x00, 0x00, 0x7C, 0x95, 0x00, 0x00, 0x65, 0x6E, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0xC7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9B, 0x78, 0x00, 0x00, 0x9C, 0x3F,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xC7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9B, 0x78,
        0x00, 0x00, 0x9C, 0x3F,
    };
    // clang-format on

    CHECK(data->size() == 68);
    CHECK(*data == kExpected);

    CHECK(dfs.exists(*path));
}

TEST_CASE("DAT2 archive decompresses a large entry", "[dat]") {
    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

    const auto path = findBySuffix(dfs, geck::test::HR_ALLTLK);
    REQUIRE(path.has_value());

    const auto data = dfs.readRawBytes(*path);
    REQUIRE(data.has_value());
    CHECK(data->size() == geck::test::HR_ALLTLK_SIZE);
    CHECK(byteSum(*data) == 29414514u);
}

TEST_CASE("DAT2 archive reports a missing file as absent", "[dat]") {
    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

    CHECK_FALSE(dfs.exists("does/not/exist.xyz"));
    CHECK_FALSE(dfs.readRawBytes("does/not/exist.xyz").has_value());
}

// Regression for the crash when selecting a map "quickly" in the browser: the background
// map-loader thread and the UI thread both read the DAT's shared reader, and an unsynchronised
// seek+read corrupted each other -> zlib inflate failed -> uncaught throw -> abort(). Reads
// through the DAT2 filesystem must therefore be safe to run concurrently.
TEST_CASE("DAT2 archive serves concurrent reads without corruption", "[dat]") {
    DataFileSystem dfs;
    dfs.addDataPath(geck::test::f2ResDat());

    // A large, zlib-compressed entry: the bigger the inflate, the more reliably a corrupted
    // concurrent read is detected.
    const auto found = geck::test::findBySuffix(dfs, geck::test::HR_ALLTLK);
    REQUIRE(found.has_value());
    const std::filesystem::path path = *found;

    // The correct content, established single-threaded.
    const auto expected = dfs.readRawBytes(path);
    REQUIRE(expected.has_value());
    const size_t expectedSize = expected->size();
    const uint64_t expectedSum = byteSum(*expected);
    REQUIRE(expectedSize == geck::test::HR_ALLTLK_SIZE);

    constexpr int kThreads = 16;
    constexpr int kReadsPerThread = 60;
    std::atomic mismatches{ 0 };

    {
        // std::thread (not std::jthread): jthread is absent from the macOS CI toolchain's
        // libc++. Join every worker explicitly before the check below.
        std::vector<std::thread> workers;
        workers.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([&dfs, &path, expectedSize, expectedSum, &mismatches] {
                for (int i = 0; i < kReadsPerThread; ++i) {
                    // readRawBytes catches read failures and returns nullopt, so a corrupted
                    // concurrent read surfaces as missing/short/wrong data rather than a throw.
                    const auto data = dfs.readRawBytes(path);
                    if (!data || data->size() != expectedSize || byteSum(*data) != expectedSum) {
                        ++mismatches;
                    }
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
    }

    CHECK(mismatches.load() == 0);
}
