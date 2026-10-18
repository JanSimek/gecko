#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "resource/DataFileSystem.h"
#include "support/TempFile.h"

namespace fs = std::filesystem;

// =============================================================================
// DataFileSystem::addDataPath tests
// =============================================================================

TEST_CASE("DataFileSystem mounts a .dat file path", "[paths][dfs]") {
    const geck::test::TempDir root("dfs_missing_dat");

    // We can't easily create a valid .dat file, but we can verify addDataPath
    // does not crash on a non-existent .dat (it should log an error and return)
    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root.path() / "nonexistent.dat");

    // Should not crash and the VFS should have no files
    REQUIRE(dfs.list("*").empty());
}

TEST_CASE("DataFileSystem skips empty resolved mount roots", "[paths][dfs]") {
    geck::resource::DataFileSystem dfs;

    // Empty path should be handled gracefully
    dfs.addDataPath(fs::path{ });
    REQUIRE(dfs.list("*").empty());
}

#ifdef __APPLE__

TEST_CASE("DataFileSystem resolves macOS .app bundle to GOG wrapper path", "[paths][dfs][macos]") {
    const geck::test::TempDir root("dfs_gog_app");

    const fs::path gogPath = root.path() / "Test.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
    geck::test::writeFile(gogPath / "data/maps/test.map", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root.path() / "Test.app");

    REQUIRE(dfs.exists("data/maps/test.map"));
}

TEST_CASE("DataFileSystem rejects macOS .app bundle with data/ but no GOG wrapper", "[paths][dfs][macos]") {
    const geck::test::TempDir root("dfs_fake_app");

    // This mimics the CrossOver wrapper case where the .app has a data/ directory
    // but no actual Fallout 2 game data in the GOG wrapper path
    geck::test::writeFile(root.path() / "Fake.app/data/maps/test.map", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root.path() / "Fake.app");

    // Should NOT mount the .app root (which would cause symlink traversal issues)
    REQUIRE_FALSE(dfs.exists("data/maps/test.map"));
}

#endif // __APPLE__

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("DataFileSystem handles multiple addDataPath calls", "[paths][dfs]") {
    const geck::test::TempDir root1("dfs_multi_1");
    const geck::test::TempDir root2("dfs_multi_2");

    geck::test::writeFile(root1.path() / "data/maps/map1.map", "");
    geck::test::writeFile(root2.path() / "data/maps/map2.map", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root1.path());
    dfs.addDataPath(root2.path());

    REQUIRE(dfs.exists("data/maps/map1.map"));
    REQUIRE(dfs.exists("data/maps/map2.map"));
}

TEST_CASE("DataFileSystem clear removes all mounted paths", "[paths][dfs]") {
    const geck::test::TempDir root("dfs_clear");

    geck::test::writeFile(root.path() / "data/maps/test.map", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root.path());
    REQUIRE(dfs.exists("data/maps/test.map"));

    dfs.clear();
    REQUIRE_FALSE(dfs.exists("data/maps/test.map"));
}
