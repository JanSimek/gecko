#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>

#include "resource/DataFileSystem.h"

namespace {

/// Create a directory tree relative to a root, including all parent directories.
void mkdirs(const QTemporaryDir& root, const QString& relativePath) {
    QDir().mkpath(root.filePath(relativePath));
}

/// Create an empty regular file, creating parent directories as needed.
void touchFile(const QTemporaryDir& root, const QString& relativePath) {
    const QString fullPath = root.filePath(relativePath);
    QDir().mkpath(QFileInfo(fullPath).path());
    QFile file(fullPath);
    file.open(QIODevice::WriteOnly);
}

/// Build a standard Fallout 2 data directory layout at the given root-relative path.
/// When mountable is true, only creates the data/ subdirectory (avoids empty .dat files
/// that would fail to parse when DataFileSystem tries to auto-mount them).
void createFallout2DataLayout(const QTemporaryDir& root, const QString& basePath = "", bool mountable = false) {
    const QString prefix = basePath.isEmpty() ? QString() : basePath + "/";
    mkdirs(root, prefix + "data");
    if (!mountable) {
        touchFile(root, prefix + "master.dat");
        touchFile(root, prefix + "critter.dat");
    }
}

std::filesystem::path rootPath(const QTemporaryDir& root) {
    return root.path().toStdString();
}

} // namespace

// =============================================================================
// DataFileSystem::addDataPath tests
// =============================================================================

TEST_CASE("DataFileSystem mounts a standard directory and can list files", "[paths][dfs]") {
    QTemporaryDir root;
    REQUIRE(root.isValid());

    createFallout2DataLayout(root, "", true);
    touchFile(root, "data/maps/arvillag.map");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(rootPath(root));

    REQUIRE(dfs.exists("data/maps/arvillag.map"));
}

TEST_CASE("DataFileSystem mounts a .dat file path", "[paths][dfs]") {
    QTemporaryDir root;
    REQUIRE(root.isValid());

    // We can't easily create a valid .dat file, but we can verify addDataPath
    // does not crash on a non-existent .dat (it should log an error and return)
    geck::resource::DataFileSystem dfs;
    const auto fakeDat = rootPath(root) / "nonexistent.dat";
    dfs.addDataPath(fakeDat);

    // Should not crash and the VFS should have no files
    REQUIRE(dfs.list("*").empty());
}

TEST_CASE("DataFileSystem skips empty resolved mount roots", "[paths][dfs]") {
    geck::resource::DataFileSystem dfs;

    // Empty path should be handled gracefully
    dfs.addDataPath(std::filesystem::path{});
    REQUIRE(dfs.list("*").empty());
}

#ifdef __APPLE__

TEST_CASE("DataFileSystem resolves macOS .app bundle to GOG wrapper path", "[paths][dfs][macos]") {
    QTemporaryDir root;
    REQUIRE(root.isValid());

    const QString gogPath = "Test.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
    createFallout2DataLayout(root, gogPath, true);
    touchFile(root, gogPath + "/data/maps/test.map");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(rootPath(root) / "Test.app");

    REQUIRE(dfs.exists("data/maps/test.map"));
}

TEST_CASE("DataFileSystem rejects macOS .app bundle with data/ but no GOG wrapper", "[paths][dfs][macos]") {
    QTemporaryDir root;
    REQUIRE(root.isValid());

    // This mimics the CrossOver wrapper case where the .app has a data/ directory
    // but no actual Fallout 2 game data in the GOG wrapper path
    mkdirs(root, "Fake.app/data");
    touchFile(root, "Fake.app/data/maps/test.map");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(rootPath(root) / "Fake.app");

    // Should NOT mount the .app root (which would cause symlink traversal issues)
    REQUIRE_FALSE(dfs.exists("data/maps/test.map"));
}

#endif // __APPLE__

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("DataFileSystem handles multiple addDataPath calls", "[paths][dfs]") {
    QTemporaryDir root1;
    QTemporaryDir root2;
    REQUIRE(root1.isValid());
    REQUIRE(root2.isValid());

    createFallout2DataLayout(root1, "", true);
    touchFile(root1, "data/maps/map1.map");

    createFallout2DataLayout(root2, "", true);
    touchFile(root2, "data/maps/map2.map");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(rootPath(root1));
    dfs.addDataPath(rootPath(root2));

    REQUIRE(dfs.exists("data/maps/map1.map"));
    REQUIRE(dfs.exists("data/maps/map2.map"));
}

TEST_CASE("DataFileSystem clear removes all mounted paths", "[paths][dfs]") {
    QTemporaryDir root;
    REQUIRE(root.isValid());

    createFallout2DataLayout(root, "", true);
    touchFile(root, "data/maps/test.map");

    geck::resource::DataFileSystem dfs;
    dfs.addDataPath(rootPath(root));
    REQUIRE(dfs.exists("data/maps/test.map"));

    dfs.clear();
    REQUIRE_FALSE(dfs.exists("data/maps/test.map"));
}
