#include <catch2/catch_test_macros.hpp>

#include "resource/DataFileSystem.h"
#include "resource/MapNameEditor.h"
#include "util/FileIo.h"

#include <filesystem>
#include <optional>
#include <string>

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
namespace fs = std::filesystem;

using geck::io::readFile;
using geck::io::writeFile;

TEST_CASE("saveMapNames writes lookup_name and/or display name to the writable overlay", "[mapnameeditor]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "mne_ok";
    fs::remove_all(base);
    const fs::path source = base / "source";
    const fs::path root = base / "writable";
    writeFile(source / "data" / "maps.txt",
        "[Map 0]\nlookup_name=Town A\nmap_name=a\n[Map 1]\nlookup_name=Town B\nmap_name=b");
    writeFile(source / "text" / "english" / "game" / "map.msg", "{200}{}{Old Display}\n{203}{}{B}");

    resource::DataFileSystem files;
    files.addDataPath(source.string());

    SECTION("lookup name only -> maps.txt") {
        const auto r = resource::saveMapNames(files, root, 0, 0, std::optional<std::string>{ "New A" }, std::nullopt);
        CHECK(r.ok);
        CHECK(readFile(root / "data" / "maps.txt").find("lookup_name=New A") != std::string::npos);
    }
    SECTION("display name only -> map.msg (id = 0*3+0+200)") {
        const auto r = resource::saveMapNames(files, root, 0, 0, std::nullopt, std::optional<std::string>{ "Wasteland" });
        CHECK(r.ok);
        CHECK(readFile(root / "text" / "english" / "game" / "map.msg").find("{200}{}{Wasteland}") != std::string::npos);
    }
    SECTION("both fields") {
        const auto r = resource::saveMapNames(files, root, 0, 0, std::optional<std::string>{ "New A" }, std::optional<std::string>{ "Wasteland" });
        CHECK(r.ok);
        CHECK(readFile(root / "data" / "maps.txt").find("lookup_name=New A") != std::string::npos);
        CHECK(readFile(root / "text" / "english" / "game" / "map.msg").find("{200}{}{Wasteland}") != std::string::npos);
    }
    fs::remove_all(base);
}

TEST_CASE("saveMapNames hard-blocks an edit that would make maps.txt invalid, writing nothing", "[mapnameeditor]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "mne_invalid";
    fs::remove_all(base);
    const fs::path source = base / "source";
    const fs::path root = base / "writable";
    // A GAP — [Map 0] + [Map 2], no [Map 1] — so the registry is already invalid.
    writeFile(source / "data" / "maps.txt",
        "[Map 0]\nlookup_name=Town A\nmap_name=a\n[Map 2]\nlookup_name=Town C\nmap_name=c");

    resource::DataFileSystem files;
    files.addDataPath(source.string());

    const auto r = resource::saveMapNames(files, root, 0, 0, std::optional<std::string>{ "Renamed A" }, std::nullopt);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    const fs::path written = root / "data" / "maps.txt";
    if (fs::exists(written)) {
        CHECK(readFile(written).find("Renamed A") == std::string::npos); // the edit was not persisted
    }
    fs::remove_all(base);
}

TEST_CASE("saveMapNames reports a missing lookup_name key (no write)", "[mapnameeditor]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "mne_nokey";
    fs::remove_all(base);
    const fs::path source = base / "source";
    const fs::path root = base / "writable";
    writeFile(source / "data" / "maps.txt", "[Map 0]\nmap_name=a"); // no lookup_name line to set

    resource::DataFileSystem files;
    files.addDataPath(source.string());

    const auto r = resource::saveMapNames(files, root, 0, 0, std::optional<std::string>{ "X" }, std::nullopt);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    fs::remove_all(base);
}
