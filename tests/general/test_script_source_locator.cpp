#include <catch2/catch_test_macros.hpp>

#include "resource/DataFileSystem.h"
#include "resource/ScriptSourceLocator.h"
#include "util/FileIo.h"

#include <filesystem>

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
using geck::io::writeFile;
namespace fs = std::filesystem;

TEST_CASE("scriptBaseName strips extension, whitespace and comments and lowercases", "[ssl]") {
    CHECK(resource::scriptBaseName("ARTEMPLE.INT") == "artemple");
    CHECK(resource::scriptBaseName("  obj_dude.int \r") == "obj_dude");
    CHECK(resource::scriptBaseName("ACBRAHMN.int ; brahmin in Arroyo") == "acbrahmn");
    CHECK(resource::scriptBaseName("noext") == "noext");
    CHECK(resource::scriptBaseName("   ") == "");
}

TEST_CASE("locateScriptSource and locateCompiledScript resolve loose files to disk paths", "[ssl]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "ssl_locator";
    fs::remove_all(base);
    const fs::path dataRoot = base / "mod";
    writeFile(dataRoot / "scripts" / "test1.ssl", "procedure start begin end");
    writeFile(dataRoot / "scripts" / "test1.int", "bytecode");
    writeFile(dataRoot / "scripts" / "source" / "test2.ssl", "procedure start begin end");

    resource::DataFileSystem files;
    files.addDataPath(dataRoot.string());

    SECTION("scripts/<base>.ssl wins and reports its native path") {
        const auto location = resource::locateScriptSource(files, "test1");
        REQUIRE(location.has_value());
        CHECK(location->vfsPath.generic_string() == "scripts/test1.ssl");
        CHECK_FALSE(location->insideDat);
        CHECK(fs::equivalent(location->diskPath, dataRoot / "scripts" / "test1.ssl"));
    }

    SECTION("scripts/source/<base>.ssl is the fallback probe") {
        const auto location = resource::locateScriptSource(files, "test2");
        REQUIRE(location.has_value());
        CHECK(location->vfsPath.generic_string() == "scripts/source/test2.ssl");
        CHECK(fs::equivalent(location->diskPath, dataRoot / "scripts" / "source" / "test2.ssl"));
    }

    SECTION("missing source and missing compiled script report nullopt") {
        CHECK_FALSE(resource::locateScriptSource(files, "test3").has_value());
        CHECK_FALSE(resource::locateCompiledScript(files, "test3").has_value());
        CHECK_FALSE(resource::locateScriptSource(files, "").has_value());
    }

    SECTION("the compiled .int resolves like the source does") {
        const auto location = resource::locateCompiledScript(files, "test1");
        REQUIRE(location.has_value());
        CHECK(location->vfsPath.generic_string() == "scripts/test1.int");
        CHECK(fs::equivalent(location->diskPath, dataRoot / "scripts" / "test1.int"));
    }

    fs::remove_all(base);
}

TEST_CASE("findScriptSourceInRoots matches by stem across an RP-style area tree", "[ssl]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "ssl_source_roots";
    fs::remove_all(base);
    const fs::path root = base / "scripts_src";
    // RP layout: sources grouped by area, compiled .int are flat elsewhere. Base name is the key.
    writeFile(root / "modoc" / "mcrat.ssl", "procedure start begin end");
    writeFile(root / "newreno" / "NcProsti.ssl", "procedure start begin end"); // mixed case on disk
    writeFile(root / "newreno" / "ncprosti.ssl.tmp", "preprocessor artefact"); // must be ignored
    writeFile(root / "headers" / "define.h", "#define FOO 1");                 // not a script

    SECTION("finds a source in an area subdir and reports the marked root as workspace") {
        const auto match = resource::findScriptSourceInRoots({ root }, "mcrat");
        REQUIRE(match.has_value());
        CHECK(fs::equivalent(match->sourceRoot, root));
        CHECK(fs::equivalent(match->file, root / "modoc" / "mcrat.ssl"));
    }

    SECTION("base-name match is case-insensitive and ignores .ssl.tmp") {
        const auto match = resource::findScriptSourceInRoots({ root }, "ncprosti");
        REQUIRE(match.has_value());
        CHECK(fs::equivalent(match->file, root / "newreno" / "NcProsti.ssl"));
    }

    SECTION("a stem with no source returns nullopt") {
        CHECK_FALSE(resource::findScriptSourceInRoots({ root }, "nosuch").has_value());
        CHECK_FALSE(resource::findScriptSourceInRoots({ root }, "").has_value());
        CHECK_FALSE(resource::findScriptSourceInRoots({}, "mcrat").has_value());
    }

    SECTION("a duplicate stem resolves deterministically and flags ambiguity") {
        writeFile(root / "vault13" / "waypnt.ssl", "a");
        writeFile(root / "ncr" / "waypnt.ssl", "b");
        bool ambiguous = false;
        const auto match = resource::findScriptSourceInRoots({ root }, "waypnt", &ambiguous);
        REQUIRE(match.has_value());
        CHECK(ambiguous);
        // Lexicographically smallest path wins → ncr/ before vault13/.
        CHECK(fs::equivalent(match->file, root / "ncr" / "waypnt.ssl"));
    }

    SECTION("roots are searched in order; an earlier root wins") {
        const fs::path root2 = base / "other_src";
        writeFile(root2 / "mcrat.ssl", "override");
        const auto match = resource::findScriptSourceInRoots({ root2, root }, "mcrat");
        REQUIRE(match.has_value());
        CHECK(fs::equivalent(match->sourceRoot, root2));
    }

    fs::remove_all(base);
}

TEST_CASE("compiledScriptTarget prefers the loose copy, then the writable root", "[ssl]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "ssl_target";
    fs::remove_all(base);
    const fs::path dataRoot = base / "mod";
    const fs::path writableRoot = base / "writable";
    writeFile(dataRoot / "scripts" / "existing.int", "bytecode");
    fs::create_directories(writableRoot);

    resource::DataFileSystem files;
    files.addDataPath(dataRoot.string());

    SECTION("an existing loose .int is overwritten in place") {
        const auto target = resource::compiledScriptTarget(files, writableRoot, "existing");
        REQUIRE(target.has_value());
        CHECK(fs::equivalent(target->parent_path(), dataRoot / "scripts"));
        CHECK(target->filename() == "existing.int");
    }

    SECTION("a new script lands under the writable root") {
        const auto target = resource::compiledScriptTarget(files, writableRoot, "fresh");
        REQUIRE(target.has_value());
        CHECK(*target == writableRoot / "scripts" / "fresh.int");
    }

    SECTION("no loose copy and no writable root -> nullopt") {
        CHECK_FALSE(resource::compiledScriptTarget(files, std::nullopt, "fresh").has_value());
    }

    SECTION("decompiled source goes next to a loose .int, else under the writable root") {
        const auto nextToInt = resource::decompiledSourceTarget(files, writableRoot, "existing");
        REQUIRE(nextToInt.has_value());
        CHECK(nextToInt->filename() == "existing.ssl");
        CHECK(fs::equivalent(nextToInt->parent_path(), dataRoot / "scripts"));

        const auto fallback = resource::decompiledSourceTarget(files, writableRoot, "fresh");
        REQUIRE(fallback.has_value());
        CHECK(*fallback == writableRoot / "scripts" / "fresh.ssl");

        CHECK_FALSE(resource::decompiledSourceTarget(files, std::nullopt, "fresh").has_value());
    }

    fs::remove_all(base);
}
