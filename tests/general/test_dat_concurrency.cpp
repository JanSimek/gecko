#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <thread>
#include <vector>

#include "resource/DataFileSystem.h"
#include "support/Fixtures.h"

using geck::resource::DataFileSystem;

namespace {

uint64_t byteSum(const std::vector<uint8_t>& data) {
    return std::accumulate(data.begin(), data.end(), uint64_t{ 0 });
}

} // namespace

// Regression for the crash when selecting a map "quickly" in the browser: the background
// map-loader thread and the UI thread both read the DAT's shared reader, and an unsynchronised
// seek+read corrupted each other -> zlib inflate failed -> uncaught throw -> abort(). Reads
// through the DAT2 filesystem must therefore be safe to run concurrently.
TEST_CASE("DAT2 archive serves concurrent reads without corruption", "[dat][vfs][concurrency]") {
    DataFileSystem dfs;
    dfs.addDataPath(geck::test::dataPath("f2_res.dat"));

    // A large, zlib-compressed entry: the bigger the inflate, the more reliably a corrupted
    // concurrent read is detected.
    const auto files = dfs.list("*");
    const auto it = std::ranges::find_if(files, [](const std::filesystem::path& f) {
        return f.generic_string().ends_with("hr_alltlk.frm");
    });
    REQUIRE(it != files.end());
    const std::filesystem::path path = *it;

    // The correct content, established single-threaded.
    const auto expected = dfs.readRawBytes(path);
    REQUIRE(expected.has_value());
    const size_t expectedSize = expected->size();
    const uint64_t expectedSum = byteSum(*expected);
    REQUIRE(expectedSize == 307274);

    constexpr int kThreads = 8;
    constexpr int kReadsPerThread = 25;
    std::atomic<int> mismatches{ 0 };

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < kReadsPerThread; ++i) {
                try {
                    const auto data = dfs.readRawBytes(path);
                    if (!data || data->size() != expectedSize || byteSum(*data) != expectedSum) {
                        mismatches.fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (...) {
                    // A corrupted concurrent read throws; count it rather than letting it
                    // escape the thread and abort the process (which is the bug being fixed).
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    CHECK(mismatches.load() == 0);
}
