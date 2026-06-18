#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>
#include <vector>

#include "format/frm/Frm.h"
#include "format/lst/Lst.h"
#include "reader/ReaderExceptions.h"
#include "resource/GameResources.h"
#include "resource/ResourceRepository.h"

namespace fs = std::filesystem;
using namespace geck;
using geck::resource::GameResources;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

// A GameResources mounting one small LST fixture, with its ResourceRepository.
struct RepoFixture {
    fs::path root;
    GameResources resources;

    // GECK_TEST_TMP_DIR (a build-tree dir, injected per target in tests/CMakeLists.txt)
    // rather than the world-writable system temp, which keeps a predictable name private.
    RepoFixture()
        : root(fs::path(GECK_TEST_TMP_DIR) / "geck_resourcerepo_test") {
        std::error_code ec;
        fs::remove_all(root, ec); // non-throwing: a stale dir from a prior run is fine to ignore
        writeFile(root / "art/tiles/tiles.lst", "floor.frm\ngrass.frm\n");
        resources.files().addDataPath(root.string());
    }
    ~RepoFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    resource::ResourceRepository& repo() { return resources.repository(); }
};

constexpr const char* LST_PATH = "art/tiles/tiles.lst";

} // namespace

TEST_CASE("ResourceRepository caches and reuses loaded resources", "[resource]") {
    RepoFixture fx;
    auto& repo = fx.repo();

    CHECK(repo.find<Lst>(LST_PATH) == nullptr); // miss before load

    Lst* first = repo.load<Lst>(LST_PATH);
    REQUIRE(first != nullptr);
    REQUIRE(first->list().size() == 2);
    CHECK(first->list()[0] == "floor.frm");

    // Hit: a second load returns the SAME cached object, not a fresh parse.
    const Lst* second = repo.load<Lst>(LST_PATH);
    CHECK(second == first);
    CHECK(repo.find<Lst>(LST_PATH) == first);
}

TEST_CASE("ResourceRepository rejects a type-mismatched lookup", "[resource]") {
    RepoFixture fx;
    auto& repo = fx.repo();
    REQUIRE(repo.load<Lst>(LST_PATH) != nullptr);

    // Cached as an Lst; requesting a Frm on the same key must throw rather than
    // silently miss (or hand back a bad cast).
    CHECK_THROWS(repo.find<Frm>(LST_PATH));
}

TEST_CASE("ResourceRepository clear() drops cached resources", "[resource]") {
    RepoFixture fx;
    auto& repo = fx.repo();
    REQUIRE(repo.load<Lst>(LST_PATH) != nullptr);
    REQUIRE(repo.find<Lst>(LST_PATH) != nullptr);

    repo.clear();
    CHECK(repo.find<Lst>(LST_PATH) == nullptr);
    CHECK(repo.load<Lst>(LST_PATH) != nullptr); // reload works after clear
}

TEST_CASE("ResourceRepository load throws on a missing file", "[resource]") {
    RepoFixture fx;
    CHECK_THROWS_AS(fx.repo().load<Lst>("art/tiles/nonexistent.lst"), geck::IOException);
}

// Background loaders (MapLoader, DataPathLoader) call load()/find() concurrently
// with the main thread. The cache must therefore tolerate concurrent access and
// still deduplicate: racing loads of one key must converge on a single object.
TEST_CASE("ResourceRepository load is thread-safe and deduplicates under contention", "[resource]") {
    const fs::path root = fs::path(GECK_TEST_TMP_DIR) / "geck_resourcerepo_concurrency";
    std::error_code ec;
    fs::remove_all(root, ec);
    // Write every fixture file before mounting so all keys are visible.
    writeFile(root / "art/tiles/tiles.lst", "floor.frm\ngrass.frm\n");
    writeFile(root / "art/tiles/a.lst", "a.frm\n");
    writeFile(root / "art/tiles/b.lst", "b.frm\n");

    GameResources resources;
    resources.files().addDataPath(root.string());
    auto& repo = resources.repository();

    const std::array<const char*, 3> keys{ LST_PATH, "art/tiles/a.lst", "art/tiles/b.lst" };
    constexpr int kThreads = 16;
    constexpr int kIterations = 250;

    std::atomic<bool> sawNull{ false };
    std::atomic<bool> sawMismatch{ false }; // load() and a same-key find() disagreed
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    // Note: Catch2 assertion macros are not thread-safe, so workers only set
    // atomics; all assertions run on the main thread after join().
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kIterations; ++i) {
                const char* key = keys[static_cast<size_t>(t + i) % keys.size()];
                Lst* loaded = repo.load<Lst>(key);
                if (loaded == nullptr) {
                    sawNull = true;
                    continue;
                }
                if (repo.find<Lst>(key) != loaded) {
                    sawMismatch = true;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    CHECK_FALSE(sawNull.load());
    CHECK_FALSE(sawMismatch.load());

    // Each key resolves to exactly one stable, deduplicated cached object.
    Lst* tiles = repo.find<Lst>(LST_PATH);
    Lst* a = repo.find<Lst>("art/tiles/a.lst");
    Lst* b = repo.find<Lst>("art/tiles/b.lst");
    REQUIRE(tiles != nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);
    CHECK(a != tiles);
    CHECK(repo.load<Lst>("art/tiles/a.lst") == a);

    fs::remove_all(root, ec);
}
