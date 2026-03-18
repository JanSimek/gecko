#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <thread>

#include "util/GameDataPathResolver.h"

namespace {

/// RAII helper for a temporary directory tree.
struct TempDir {
    std::filesystem::path root;

    TempDir() {
        root = std::filesystem::temp_directory_path() / ("geck_test_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(root);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void mkdirs(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);
}

void touchFile(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path).put('\0');
}

void createFallout2DataLayout(const std::filesystem::path& base) {
    mkdirs(base / "data");
    touchFile(base / "master.dat");
    touchFile(base / "critter.dat");
}

} // namespace

// =============================================================================
// hasFallout2DataLayout
// =============================================================================

TEST_CASE("hasFallout2DataLayout returns false for non-existent path", "[paths]") {
    REQUIRE_FALSE(geck::util::hasFallout2DataLayout("/nonexistent/path"));
}

TEST_CASE("hasFallout2DataLayout returns false for empty directory", "[paths]") {
    TempDir tmp;
    REQUIRE_FALSE(geck::util::hasFallout2DataLayout(tmp.root));
}

TEST_CASE("hasFallout2DataLayout detects data/ subdirectory", "[paths]") {
    TempDir tmp;
    mkdirs(tmp.root / "data");
    REQUIRE(geck::util::hasFallout2DataLayout(tmp.root));
}

TEST_CASE("hasFallout2DataLayout detects master.dat", "[paths]") {
    TempDir tmp;
    touchFile(tmp.root / "master.dat");
    REQUIRE(geck::util::hasFallout2DataLayout(tmp.root));
}

TEST_CASE("hasFallout2DataLayout detects critter.dat", "[paths]") {
    TempDir tmp;
    touchFile(tmp.root / "critter.dat");
    REQUIRE(geck::util::hasFallout2DataLayout(tmp.root));
}

TEST_CASE("hasFallout2DataLayout detects patch000.dat", "[paths]") {
    TempDir tmp;
    touchFile(tmp.root / "patch000.dat");
    REQUIRE(geck::util::hasFallout2DataLayout(tmp.root));
}

// =============================================================================
// resolveGameDataRoot
// =============================================================================

TEST_CASE("resolveGameDataRoot returns nullopt for empty path", "[paths]") {
    REQUIRE_FALSE(geck::util::resolveGameDataRoot(std::filesystem::path{}).has_value());
}

TEST_CASE("resolveGameDataRoot returns .dat files as-is", "[paths]") {
    TempDir tmp;
    touchFile(tmp.root / "master.dat");
    const auto datPath = tmp.root / "master.dat";

    auto result = geck::util::resolveGameDataRoot(datPath);
    REQUIRE(result.has_value());
    REQUIRE(*result == datPath);
}

TEST_CASE("resolveGameDataRoot returns standard Fallout 2 directory unchanged", "[paths]") {
    TempDir tmp;
    createFallout2DataLayout(tmp.root);

    auto result = geck::util::resolveGameDataRoot(tmp.root);
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.root);
}

TEST_CASE("resolveGameDataRoot strips trailing 'data' directory component", "[paths]") {
    TempDir tmp;
    createFallout2DataLayout(tmp.root);

    auto result = geck::util::resolveGameDataRoot(tmp.root / "data");
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.root);
}

TEST_CASE("resolveGameDataRoot returns nullopt for unrecognized directory", "[paths]") {
    TempDir tmp;
    // Empty directory — no data layout
    REQUIRE_FALSE(geck::util::resolveGameDataRoot(tmp.root).has_value());
}

TEST_CASE("resolveGameDataRoot handles non-existent paths", "[paths]") {
    const std::filesystem::path bogus = "/nonexistent/path/to/nowhere";
    REQUIRE_FALSE(geck::util::resolveGameDataRoot(bogus).has_value());
}

TEST_CASE("resolveGameDataRoot: directory named 'Resources' with valid layout resolves to itself", "[paths]") {
    TempDir tmp;
    const auto resourcesDir = tmp.root / "Resources";
    createFallout2DataLayout(resourcesDir);

    auto result = geck::util::resolveGameDataRoot(resourcesDir);
    REQUIRE(result.has_value());
    // Should NOT be treated as a macOS bundle — it resolves to itself
    REQUIRE(*result == resourcesDir);
}

// =============================================================================
// pathsEquivalent
// =============================================================================

TEST_CASE("pathsEquivalent detects identical paths", "[paths]") {
    TempDir tmp;
    createFallout2DataLayout(tmp.root);
    REQUIRE(geck::util::pathsEquivalent(tmp.root, tmp.root));
}

TEST_CASE("pathsEquivalent detects equivalent paths via resolution", "[paths]") {
    TempDir tmp;
    createFallout2DataLayout(tmp.root);
    // tmp.root/data should resolve to tmp.root
    REQUIRE(geck::util::pathsEquivalent(tmp.root, tmp.root / "data"));
}

TEST_CASE("pathsEquivalent returns false for unrelated paths", "[paths]") {
    TempDir tmp;
    mkdirs(tmp.root / "a");
    mkdirs(tmp.root / "b");
    REQUIRE_FALSE(geck::util::pathsEquivalent(tmp.root / "a", tmp.root / "b"));
}

// =============================================================================
// macOS bundle tests
// =============================================================================

#ifdef __APPLE__

TEST_CASE("resolveGameDataRoot resolves macOS .app GOG bundle to wrapped game root", "[paths][macos]") {
    TempDir tmp;
    const std::filesystem::path gogPath = "Fallout 2.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
    createFallout2DataLayout(tmp.root / gogPath);

    auto result = geck::util::resolveGameDataRoot(tmp.root / "Fallout 2.app");
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.root / gogPath);
}

TEST_CASE("resolveGameDataRoot rejects macOS .app bundle without GOG wrapper", "[paths][macos]") {
    TempDir tmp;
    mkdirs(tmp.root / "Fallout 2.app/data");
    mkdirs(tmp.root / "Fallout 2.app/Contents/Resources");

    auto result = geck::util::resolveGameDataRoot(tmp.root / "Fallout 2.app");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("resolveGameDataRoot resolves Contents/Resources path to GOG wrapper", "[paths][macos]") {
    TempDir tmp;
    const std::filesystem::path gogPath = "MyApp.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
    createFallout2DataLayout(tmp.root / gogPath);

    auto result = geck::util::resolveGameDataRoot(tmp.root / "MyApp.app/Contents/Resources");
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.root / gogPath);
}

TEST_CASE("resolveGameDataRoot rejects Contents/Resources path without GOG wrapper", "[paths][macos]") {
    TempDir tmp;
    mkdirs(tmp.root / "MyApp.app/Contents/Resources");

    auto result = geck::util::resolveGameDataRoot(tmp.root / "MyApp.app/Contents/Resources");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("resolveGameDataRoot: 'Resources' not inside .app/Contents is not treated as bundle", "[paths][macos]") {
    TempDir tmp;
    // SomeDir/Contents/Resources — but SomeDir doesn't have .app extension
    const auto resourcesDir = tmp.root / "SomeDir/Contents/Resources";
    createFallout2DataLayout(resourcesDir);

    auto result = geck::util::resolveGameDataRoot(resourcesDir);
    REQUIRE(result.has_value());
    // Should resolve to itself (valid data layout), not be treated as a bundle
    REQUIRE(*result == resourcesDir);
}

#endif // __APPLE__
