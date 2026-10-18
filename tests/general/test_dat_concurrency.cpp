#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "resource/DataFileSystem.h"
#include "support/Fixtures.h"
#include "support/TempFile.h"
#include "support/VfsLookup.h"

using geck::resource::DataFileSystem;
using geck::test::byteSum;

// Regression for the crash when selecting a map "quickly" in the browser: the background
// map-loader thread and the UI thread both read the DAT's shared reader, and an unsynchronised
// seek+read corrupted each other -> zlib inflate failed -> uncaught throw -> abort(). Reads
// through the DAT2 filesystem must therefore be safe to run concurrently.
TEST_CASE("DAT2 archive serves concurrent reads without corruption", "[dat][vfs][concurrency]") {
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

// Regression for "edited .gam resets on reload": vfspp's NativeFileSystem builds its file list once at
// mount and never rescans, so a file written to a mounted directory afterwards (e.g. a saved .gam under
// a writable data path) stays invisible to readRawBytes — the archived copy is read back instead.
// refresh() must re-expose it.
TEST_CASE("DataFileSystem::refresh exposes files written after mount", "[vfs][refresh]") {
    const geck::test::TempDir tmp{ "geck_refresh_test" };
    const auto& root = tmp.path();

    // A file present at mount time is in the native filesystem's cached listing.
    geck::test::writeFile(root / "maps" / "initial.gam", "MAP_GLOBAL_VARS:\nMVAR_A := 1;\n");

    DataFileSystem dfs;
    dfs.addDataPath(root);
    REQUIRE(dfs.readRawBytes("maps/initial.gam").has_value()); // the mount is live and initialised

    // A file created AFTER the mount is invisible until a rescan.
    geck::test::writeFile(root / "maps" / "test.gam", "MAP_GLOBAL_VARS:\nMVAR_B := 7;\n");
    CHECK_FALSE(dfs.readRawBytes("maps/test.gam").has_value());

    dfs.refresh();
    const auto bytes = dfs.readRawBytes("maps/test.gam");
    REQUIRE(bytes.has_value());
    CHECK(std::string(bytes->begin(), bytes->end()).find("MVAR_B := 7;") != std::string::npos);
}
