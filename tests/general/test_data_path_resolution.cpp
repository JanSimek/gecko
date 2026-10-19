#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "resource/DataFileSystem.h"
#include "support/TempFile.h"

namespace fs = std::filesystem;

// =============================================================================
// DataFileSystem::addDataPath tests
// =============================================================================

TEST_CASE("DataFileSystem mounts a .dat file path", "[data_file_system]") {
    const geck::test::TempDir root("dfs_missing_dat");

    // We can't easily create a valid .dat file, but we can verify addDataPath
    // does not crash on a non-existent .dat (it should log an error and return)
    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root.path() / "nonexistent.dat");

    // Should not crash and the VFS should have no files
    CHECK(dfs.list("*").empty());
}

TEST_CASE("DataFileSystem skips empty resolved mount roots", "[data_file_system]") {
    geck::resource::DataFileSystem dfs;

    // Empty path should be handled gracefully
    dfs.addDataPath(fs::path{ });
    CHECK(dfs.list("*").empty());
}

#ifdef __APPLE__

// Integration: the mount goes through util::resolveGameDataRoot (its bundle cases are covered in
// test_game_data_path_resolver), so a GOG .app mounts its wrapped game root and nothing else.
TEST_CASE("DataFileSystem mounts a macOS .app bundle only through its GOG wrapper", "[data_file_system][macos]") {
    const geck::test::TempDir root("dfs_app");

    SECTION("the wrapped game root is mounted") {
        const fs::path gogPath = root.path() / "Test.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
        geck::test::writeFile(gogPath / "data/maps/test.map", "");

        geck::resource::DataFileSystem dfs;
        dfs.addDataPath(root.path() / "Test.app");

        CHECK(dfs.exists("data/maps/test.map"));
    }

    SECTION("a bundle with data/ but no GOG wrapper is not mounted") {
        // This mimics the CrossOver wrapper case where the .app has a data/ directory but no actual
        // Fallout 2 game data in the GOG wrapper path. Mounting the .app root would cause symlink
        // traversal issues.
        geck::test::writeFile(root.path() / "Fake.app/data/maps/test.map", "");

        geck::resource::DataFileSystem dfs;
        dfs.addDataPath(root.path() / "Fake.app");

        CHECK_FALSE(dfs.exists("data/maps/test.map"));
    }
}

#endif // __APPLE__

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("DataFileSystem handles multiple addDataPath calls", "[data_file_system]") {
    const geck::test::TempDir root1("dfs_multi_1");
    const geck::test::TempDir root2("dfs_multi_2");

    geck::test::writeFile(root1.path() / "data/maps/map1.map", "");
    geck::test::writeFile(root2.path() / "data/maps/map2.map", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root1.path());
    dfs.addDataPath(root2.path());

    CHECK(dfs.exists("data/maps/map1.map"));
    CHECK(dfs.exists("data/maps/map2.map"));
}

TEST_CASE("DataFileSystem clear removes all mounted paths", "[data_file_system]") {
    const geck::test::TempDir root("dfs_clear");

    geck::test::writeFile(root.path() / "data/maps/test.map", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(root.path());
    REQUIRE(dfs.exists("data/maps/test.map"));

    dfs.clear();
    CHECK_FALSE(dfs.exists("data/maps/test.map"));
}

TEST_CASE("DataFileSystem mounts a data tree as given", "[data_file_system]") {
    const geck::test::TempDir tmp{ "dfs_layout" };
    geck::test::writeFile(tmp.path() / "data/maps.txt", ""); // the data tree's own data/maps.txt
    geck::test::writeFile(tmp.path() / "proto/scenery/scenery.lst", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(tmp.path());

    CHECK(dfs.exists("data/maps.txt"));
    CHECK(dfs.exists("proto/scenery/scenery.lst"));
}

// Regression: choosing the install folder as a data path mounted the folder itself, so its loose
// `data` files landed at "/data/proto/..." - a path nothing looks up - and master.dat answered
// instead. A Restoration Project map then hit PIDs past the end of vanilla's proto lists and failed
// to load at all ("PID index out of range").
TEST_CASE("DataFileSystem mounts an install folder's loose data files", "[data_file_system]") {
    const geck::test::TempDir tmp{ "dfs_layout" };
    geck::test::writeFile(tmp.path() / "master.dat", "");
    geck::test::writeFile(tmp.path() / "data/proto/scenery/scenery.lst", "");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(tmp.path());

    CHECK(dfs.exists("proto/scenery/scenery.lst"));
}

// Regression for "edited .gam resets on reload": vfspp's NativeFileSystem builds its file list once at
// mount and never rescans, so a file written to a mounted directory afterwards (e.g. a saved .gam under
// a writable data path) stays invisible to readRawBytes — the archived copy is read back instead.
// refresh() must re-expose it.
TEST_CASE("DataFileSystem::refresh exposes files written after mount", "[data_file_system]") {
    const geck::test::TempDir tmp{ "geck_refresh_test" };
    const auto& root = tmp.path();

    // A file present at mount time is in the native filesystem's cached listing.
    geck::test::writeFile(root / "maps" / "initial.gam", "MAP_GLOBAL_VARS:\nMVAR_A := 1;\n");

    geck::resource::DataFileSystem dfs;
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
