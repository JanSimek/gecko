#include <catch2/catch_test_macros.hpp>

#include "resource/DataFileSystem.h"
#include "resource/WritableDataRoot.h"
#include "util/FileIo.h"

#include <filesystem>
#include <string>

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
using geck::io::readFile;
using geck::io::writeFile;
namespace fs = std::filesystem;

TEST_CASE("ensureWritableCopy copies a mounted file out (CRLF preserved) and is idempotent", "[writableroot]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "wr_test";
    fs::remove_all(base);
    const fs::path source = base / "source";
    const fs::path root = base / "writable";
    writeFile(source / "data" / "maps.txt", "HELLO\r\nWORLD\r\n");

    resource::DataFileSystem files;
    files.addDataPath(source.string());

    const fs::path dest = resource::ensureWritableCopy(files, root, "data/maps.txt");
    CHECK(dest == root / "data" / "maps.txt");
    CHECK(readFile(dest) == "HELLO\r\nWORLD\r\n"); // bytes, including CRLF, verbatim

    // Idempotent: an existing (edited) copy is returned untouched, not re-copied from the source.
    writeFile(dest, "EDITED\r\n");
    const fs::path again = resource::ensureWritableCopy(files, root, "data/maps.txt");
    CHECK(again == dest);
    CHECK(readFile(again) == "EDITED\r\n");

    fs::remove_all(base);
}

TEST_CASE("ensureWritableCopy throws when the file is not in the mounted data", "[writableroot]") {
    const fs::path root = fs::path{ GECK_TEST_TMP_DIR } / "wr_empty";
    fs::remove_all(root);
    resource::DataFileSystem files; // nothing mounted
    CHECK_THROWS(resource::ensureWritableCopy(files, root, "data/nope.txt"));
}

TEST_CASE("findWritableDataPath returns the last directory, skipping archives", "[writableroot]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "fwdp";
    fs::remove_all(base);
    const fs::path dirA = base / "a";
    const fs::path dirB = base / "b";
    fs::create_directories(dirA);
    fs::create_directories(dirB);
    const fs::path fakeDat = base / "master.dat";
    writeFile(fakeDat, "not a real dat"); // a file, not a directory

    SECTION("last directory wins; archive files are skipped") {
        const auto target = resource::findWritableDataPath({ dirA, fakeDat, dirB });
        REQUIRE(target.has_value());
        CHECK(*target == dirB);
    }
    SECTION("a trailing archive is skipped for the last directory before it") {
        const auto target = resource::findWritableDataPath({ dirA, dirB, fakeDat });
        REQUIRE(target.has_value());
        CHECK(*target == dirB);
    }
    SECTION("no directory among the data paths -> nullopt") {
        CHECK_FALSE(resource::findWritableDataPath({ fakeDat }).has_value());
        CHECK_FALSE(resource::findWritableDataPath({}).has_value());
    }
    fs::remove_all(base);
}
