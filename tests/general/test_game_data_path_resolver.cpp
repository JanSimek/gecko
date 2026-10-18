#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "util/GameDataPathResolver.h"

#include "support/TempFile.h"

using geck::test::writeFile;

namespace {

void mkdirs(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);
}

void createFallout2DataLayout(const std::filesystem::path& base) {
    mkdirs(base / "data");
    writeFile(base / "master.dat", "");
    writeFile(base / "critter.dat", "");
}

} // namespace

// =============================================================================
// hasFallout2DataLayout
// =============================================================================

TEST_CASE("hasFallout2DataLayout recognises a data/ folder or any engine archive", "[paths]") {
    CHECK_FALSE(geck::util::hasFallout2DataLayout("/nonexistent/path"));

    // The one entry created in an otherwise empty folder ("" creates nothing; a trailing '/' a folder).
    const auto [entry, expected] = GENERATE(table<std::string, bool>({
        { "", false },
        { "data/", true },
        { "master.dat", true },
        { "critter.dat", true },
        { "patch000.dat", true },
    }));
    INFO("entry: " << entry);
    const geck::test::TempDir tmp{ "gdpr" };
    if (entry.ends_with('/')) {
        mkdirs(tmp.path() / entry);
    } else if (!entry.empty()) {
        writeFile(tmp.path() / entry, "");
    }
    CHECK(geck::util::hasFallout2DataLayout(tmp.path()) == expected);
}

// =============================================================================
// looseDataDirectory
// =============================================================================

TEST_CASE("looseDataDirectory is the install root's data folder", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    createFallout2DataLayout(tmp.path());

    REQUIRE(geck::util::looseDataDirectory(tmp.path()) == tmp.path() / "data");
}

TEST_CASE("looseDataDirectory falls back to the root of a DAT-only install", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    writeFile(tmp.path() / "master.dat", "");

    REQUIRE(geck::util::looseDataDirectory(tmp.path()) == tmp.path());
}

// Appending `data` is only safe because a resolved game root is never a data folder itself, and a
// data folder is only recognisable by its company: it holds a `data` folder of its own (ai.txt,
// city.txt, ...) and so satisfies hasFallout2DataLayout exactly like an install root does.
TEST_CASE("a data folder resolves to its install root even though it looks like one", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    createFallout2DataLayout(tmp.path());
    mkdirs(tmp.path() / "data/data");
    mkdirs(tmp.path() / "data/proto");

    const auto root = geck::util::resolveGameDataRoot(tmp.path() / "data");
    REQUIRE(root.has_value());
    REQUIRE(*root == tmp.path());
    REQUIRE(geck::util::looseDataDirectory(*root) == tmp.path() / "data"); // and back, not data/data
}

// The counterpart to the append: a folder that ships loose data WITHOUT the engine's archives is the
// data itself (a standalone data tree, or a mod overlay adding files under data/). Descending on its
// `data` folder would bury art/, proto/ and the rest.
TEST_CASE("looseDataDirectory leaves a data tree that is not an install alone", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    mkdirs(tmp.path() / "art/scenery");
    mkdirs(tmp.path() / "data"); // its own data/ (ai.txt, city.txt, ...), not an install's

    REQUIRE(geck::util::looseDataDirectory(tmp.path()) == tmp.path());
}

// =============================================================================
// resolveGameDataRoot
// =============================================================================

TEST_CASE("resolveGameDataRoot rejects an empty, unrecognised or missing path", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" }; // empty: no data layout
    CHECK_FALSE(geck::util::resolveGameDataRoot(std::filesystem::path{}).has_value());
    CHECK_FALSE(geck::util::resolveGameDataRoot(tmp.path()).has_value());
    CHECK_FALSE(geck::util::resolveGameDataRoot("/nonexistent/path/to/nowhere").has_value());
}

TEST_CASE("resolveGameDataRoot returns .dat files as-is", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    writeFile(tmp.path() / "master.dat", "");
    const auto datPath = tmp.path() / "master.dat";

    auto result = geck::util::resolveGameDataRoot(datPath);
    REQUIRE(result.has_value());
    REQUIRE(*result == datPath);
}

TEST_CASE("resolveGameDataRoot returns standard Fallout 2 directory unchanged", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    createFallout2DataLayout(tmp.path());

    auto result = geck::util::resolveGameDataRoot(tmp.path());
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.path());
}

TEST_CASE("resolveGameDataRoot strips trailing 'data' directory component", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    createFallout2DataLayout(tmp.path());

    auto result = geck::util::resolveGameDataRoot(tmp.path() / "data");
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.path());
}

TEST_CASE("resolveGameDataRoot: directory named 'Resources' with valid layout resolves to itself", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    const auto resourcesDir = tmp.path() / "Resources";
    createFallout2DataLayout(resourcesDir);

    auto result = geck::util::resolveGameDataRoot(resourcesDir);
    REQUIRE(result.has_value());
    // Should NOT be treated as a macOS bundle — it resolves to itself
    REQUIRE(*result == resourcesDir);
}

// =============================================================================
// pathsEquivalent
// =============================================================================

TEST_CASE("pathsEquivalent compares paths by the game root they resolve to", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };

    SECTION("identical paths") {
        createFallout2DataLayout(tmp.path());
        CHECK(geck::util::pathsEquivalent(tmp.path(), tmp.path()));
    }

    SECTION("an install root and its data folder") {
        createFallout2DataLayout(tmp.path());
        CHECK(geck::util::pathsEquivalent(tmp.path(), tmp.path() / "data"));
    }

    SECTION("unrelated folders") {
        mkdirs(tmp.path() / "a");
        mkdirs(tmp.path() / "b");
        CHECK_FALSE(geck::util::pathsEquivalent(tmp.path() / "a", tmp.path() / "b"));
    }
}

// =============================================================================
// expandDataPaths
// =============================================================================

TEST_CASE("expandDataPaths makes a folder's DATs explicit, ordered entries", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    writeFile(tmp.path() / "master.dat", "x");
    writeFile(tmp.path() / "critter.dat", "x");

    // The folder alone, or with one of its DATs already listed: the explicit duplicate is dropped.
    const bool listMasterToo = GENERATE(false, true);
    std::vector<std::filesystem::path> in{ tmp.path() };
    if (listMasterToo) {
        in.push_back(tmp.path() / "master.dat");
    }

    const auto out = geck::util::expandDataPaths(in);
    REQUIRE(out.size() == 3);
    CHECK(out[0] == tmp.path() / "master.dat"); // DATs first so the folder's loose files (last) override them
    CHECK(out[1] == tmp.path() / "critter.dat");
    CHECK(out[2] == tmp.path());
}

TEST_CASE("expandDataPaths passes a .dat entry through unchanged", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    writeFile(tmp.path() / "master.dat", "x");
    const auto datEntry = tmp.path() / "master.dat";

    const auto out = geck::util::expandDataPaths({ datEntry });
    REQUIRE(out.size() == 1);
    CHECK(out[0] == datEntry);
}

TEST_CASE("expandDataPaths leaves a DAT-less folder alone", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    const auto out = geck::util::expandDataPaths({ tmp.path() });
    REQUIRE(out.size() == 1);
    CHECK(out[0] == tmp.path());
}

// =============================================================================
// macOS bundle tests
// =============================================================================

#ifdef __APPLE__

TEST_CASE("resolveGameDataRoot resolves macOS .app GOG bundle to wrapped game root", "[paths][macos]") {
    const geck::test::TempDir tmp{ "gdpr" };
    const std::filesystem::path gogPath = "Fallout 2.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
    createFallout2DataLayout(tmp.path() / gogPath);

    auto result = geck::util::resolveGameDataRoot(tmp.path() / "Fallout 2.app");
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.path() / gogPath);
}

TEST_CASE("resolveGameDataRoot rejects macOS .app bundle without GOG wrapper", "[paths][macos]") {
    const geck::test::TempDir tmp{ "gdpr" };
    mkdirs(tmp.path() / "Fallout 2.app/data");
    mkdirs(tmp.path() / "Fallout 2.app/Contents/Resources");

    auto result = geck::util::resolveGameDataRoot(tmp.path() / "Fallout 2.app");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("resolveGameDataRoot resolves Contents/Resources path to GOG wrapper", "[paths][macos]") {
    const geck::test::TempDir tmp{ "gdpr" };
    const std::filesystem::path gogPath = "MyApp.app/Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
    createFallout2DataLayout(tmp.path() / gogPath);

    auto result = geck::util::resolveGameDataRoot(tmp.path() / "MyApp.app/Contents/Resources");
    REQUIRE(result.has_value());
    REQUIRE(*result == tmp.path() / gogPath);
}

TEST_CASE("resolveGameDataRoot rejects Contents/Resources path without GOG wrapper", "[paths][macos]") {
    const geck::test::TempDir tmp{ "gdpr" };
    mkdirs(tmp.path() / "MyApp.app/Contents/Resources");

    auto result = geck::util::resolveGameDataRoot(tmp.path() / "MyApp.app/Contents/Resources");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("resolveGameDataRoot: 'Resources' not inside .app/Contents is not treated as bundle", "[paths][macos]") {
    const geck::test::TempDir tmp{ "gdpr" };
    // SomeDir/Contents/Resources — but SomeDir doesn't have .app extension
    const auto resourcesDir = tmp.path() / "SomeDir/Contents/Resources";
    createFallout2DataLayout(resourcesDir);

    auto result = geck::util::resolveGameDataRoot(resourcesDir);
    REQUIRE(result.has_value());
    // Should resolve to itself (valid data layout), not be treated as a bundle
    REQUIRE(*result == resourcesDir);
}

#endif // __APPLE__

TEST_CASE("ensureFallbackDataPath inserts a missing fallback at the lowest priority", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    const auto gameDir = tmp.path() / "game";
    const auto resourcesDir = tmp.path() / "resources";
    mkdirs(gameDir);
    mkdirs(resourcesDir);

    std::vector<std::filesystem::path> paths = { gameDir, gameDir / "master.dat" };
    geck::util::ensureFallbackDataPath(paths, resourcesDir);

    REQUIRE(paths.size() == 3);
    // Stored order is lowest-priority-first (the VFS resolves last-mounted-wins), so the
    // gap-filling fallback must land at the front — user data keeps overriding it.
    REQUIRE(paths.front() == resourcesDir);
    REQUIRE(paths.back() == gameDir / "master.dat");
}

TEST_CASE("ensureFallbackDataPath does not duplicate an already-listed fallback", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    const auto resourcesDir = tmp.path() / "resources";
    mkdirs(resourcesDir);

    SECTION("exact same path") {
        std::vector<std::filesystem::path> paths = { resourcesDir };
        geck::util::ensureFallbackDataPath(paths, resourcesDir);
        REQUIRE(paths.size() == 1);
    }

    SECTION("non-normal form of the same directory") {
        std::vector<std::filesystem::path> paths = { tmp.path() / "." / "resources" };
        geck::util::ensureFallbackDataPath(paths, resourcesDir);
        REQUIRE(paths.size() == 1);
    }
}

TEST_CASE("ensureFallbackDataPath skips a fallback directory that does not exist", "[paths]") {
    const geck::test::TempDir tmp{ "gdpr" };
    std::vector<std::filesystem::path> paths = { tmp.path() };
    geck::util::ensureFallbackDataPath(paths, tmp.path() / "no_such_dir");
    REQUIRE(paths.size() == 1);
}
