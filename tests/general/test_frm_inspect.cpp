#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "cli/FrmInspect.h"
#include "resource/GameResources.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace geck;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// A temp data tree with minimal art LSTs, mounted into a GameResources — the same LSTs the
// FrmResolver and the frm tools read. No FRM bytes are written: these tests cover the non-GL
// path-resolution / FID-decode / listing logic, not the (GL-bound) pixel render.
struct FixtureResources {
    fs::path root;
    resource::GameResources resources;

    FixtureResources() {
        root = fs::temp_directory_path() / "geck_frminspect_test";
        std::error_code ec;
        fs::remove_all(root, ec);

        writeFile(root / "art/items/items.lst", "knife.frm\nrock.frm\n");
        writeFile(root / "art/scenery/scenery.lst", "tree1.frm\ntree2.frm\n");
        writeFile(root / "art/walls/walls.lst", "wall1.frm\nwall2.frm\n");
        writeFile(root / "art/tiles/tiles.lst", "floor.frm\ngrass.frm\n");
        // misc.lst: index 0 'thing', a filler at index 1 (FrmResolver::resolve treats misc baseId 1 as
        // the scroll-blocker special case, so keep the test entries off it), then the exit-grid family
        // so the glob/list/resolve cases have a real group. ext2grd1 is index 2 here.
        writeFile(root / "art/misc/misc.lst", "thing.frm\nscrblk.frm\next2grd1.frm\next2grd2.frm\nexitgrd1.frm\n");

        resources.files().addDataPath(root.string());
    }

    ~FixtureResources() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

} // namespace

TEST_CASE("parseFid accepts hex and decimal, rejects junk", "[frm][cli]") {
    using cli::parseFid;
    CHECK(parseFid("0x05000021") == 0x05000021u);
    CHECK(parseFid("83886113") == 0x05000021u); // same value, decimal
    CHECK(parseFid("0") == 0u);
    CHECK_FALSE(parseFid("ext2grd1").has_value()); // a name, not a number
    CHECK_FALSE(parseFid("0x05g").has_value());    // trailing non-hex
    CHECK_FALSE(parseFid("12abc").has_value());    // trailing junk after a number
    CHECK_FALSE(parseFid("-1").has_value());       // a sign would silently wrap to a huge FID
    CHECK_FALSE(parseFid("+5").has_value());
    CHECK_FALSE(parseFid("").has_value());
}

TEST_CASE("normalizeArtToken canonicalizes path + extension", "[frm][cli]") {
    using cli::normalizeArtToken;
    // Adds .frm when missing, lowercases, flips backslashes, strips a leading slash.
    CHECK(normalizeArtToken("ext2grd1") == "ext2grd1.frm");
    CHECK(normalizeArtToken("EXT2GRD1") == "ext2grd1.frm");
    CHECK(normalizeArtToken("art/misc/ext2grd1.frm") == "art/misc/ext2grd1.frm");
    CHECK(normalizeArtToken("ART\\MISC\\EXT2GRD1.FRM") == "art/misc/ext2grd1.frm");
    CHECK(normalizeArtToken("/art/misc/ext2grd1.frm") == "art/misc/ext2grd1.frm");
}

TEST_CASE("resolveFrmTarget finds a bare name across art LSTs", "[frm][cli]") {
    FixtureResources fx;
    std::string error;

    const auto miscName = cli::resolveFrmTarget(fx.resources, "ext2grd1", error);
    REQUIRE(miscName.has_value());
    CHECK(*miscName == "art/misc/ext2grd1.frm");

    const auto sceneryName = cli::resolveFrmTarget(fx.resources, "tree2", error);
    REQUIRE(sceneryName.has_value());
    CHECK(*sceneryName == "art/scenery/tree2.frm");

    // A name not in any LST fails (no heuristic fallback), and sets an explanatory error.
    const auto missing = cli::resolveFrmTarget(fx.resources, "doesnotexist", error);
    CHECK_FALSE(missing.has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("resolveFrmTarget resolves a FID and a full art path", "[frm][cli]") {
    FixtureResources fx;
    std::string error;

    // ext2grd1 is misc index 2 -> FID 0x05000002 (index 1 is the scroll-blocker special case).
    const auto byFid = cli::resolveFrmTarget(fx.resources, "0x05000002", error);
    REQUIRE(byFid.has_value());
    CHECK(*byFid == "art/misc/ext2grd1.frm");

    // A full path under a known art dir is taken as-is (normalized).
    const auto byPath = cli::resolveFrmTarget(fx.resources, "art/walls/wall1.frm", error);
    REQUIRE(byPath.has_value());
    CHECK(*byPath == "art/walls/wall1.frm");
}

TEST_CASE("resolveFidCommand emits decoded JSON for a FID", "[frm][cli]") {
    FixtureResources fx;
    std::ostringstream oss;
    // 0x05000002: type byte 5 (misc), index 2 -> ext2grd1 in the fixture misc.lst.
    const int rc = cli::resolveFidCommand(fx.resources, "0x05000002", oss);
    CHECK(rc == 0);
    const json decoded = json::parse(oss.str());
    CHECK(decoded["type"] == "misc");
    CHECK(decoded["index"] == 2);
    CHECK(decoded["fid"] == 0x05000002);
    CHECK(decoded["artPath"] == "art/misc/ext2grd1.frm");
}

TEST_CASE("resolveFidCommand rejects a non-numeric token", "[frm][cli]") {
    FixtureResources fx;
    std::ostringstream oss;
    CHECK(cli::resolveFidCommand(fx.resources, "notafid", oss) != 0);
}

TEST_CASE("listFrms matches a glob across the art LSTs", "[frm][cli]") {
    FixtureResources fx;

    std::ostringstream ext;
    CHECK(cli::listFrms(fx.resources, "ext2grd*", ext) == 0);
    const json extList = json::parse(ext.str());
    REQUIRE(extList.size() == 2);
    CHECK(extList[0]["name"] == "ext2grd1");
    CHECK(extList[0]["artPath"] == "art/misc/ext2grd1.frm");
    CHECK(extList[0]["fid"] == 0x05000002); // misc index 2 (index 1 is the scroll-blocker)
    CHECK(extList[1]["name"] == "ext2grd2");

    // '?' matches exactly one character; 'tree?' matches tree1/tree2 but not a longer name.
    std::ostringstream trees;
    CHECK(cli::listFrms(fx.resources, "tree?", trees) == 0);
    CHECK(json::parse(trees.str()).size() == 2);

    // A non-matching glob is an empty array (success, not an error).
    std::ostringstream none;
    CHECK(cli::listFrms(fx.resources, "zzz*", none) == 0);
    CHECK(json::parse(none.str()).empty());
}

TEST_CASE("frmInfo reports a clear error for an unresolvable target", "[frm][cli]") {
    FixtureResources fx;
    std::ostringstream oss;
    CHECK(cli::frmInfo(fx.resources, "nope", oss) != 0);
    CHECK(oss.str().find("could not resolve") != std::string::npos);
}
