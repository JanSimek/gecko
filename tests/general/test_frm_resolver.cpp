#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "format/frm/Frm.h"
#include "resource/FrmResolver.h"
#include "resource/GameResources.h"

namespace fs = std::filesystem;
using geck::Frm;
using geck::resource::GameResources;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// A temp data tree with one minimal LST per art type, mounted into a GameResources.
// resolveFid reads these LSTs the same way it reads the shipped game archives.
struct FixtureResources {
    fs::path root;
    GameResources resources;

    FixtureResources() {
        root = fs::temp_directory_path() / "geck_frmresolver_test";
        std::error_code ec;
        fs::remove_all(root, ec);

        writeFile(root / "art/items/items.lst", "knife.frm\nrock.frm\n");
        writeFile(root / "art/critters/critters.lst", "hmwarr,Tribal Warrior\nnmwarr,Raider\n");
        writeFile(root / "art/scenery/scenery.lst", "tree.frm\nbush.frm\n");
        writeFile(root / "art/walls/walls.lst", "wall1.frm\nwall2.frm\nwall3.frm\n");
        writeFile(root / "art/tiles/tiles.lst", "floor.frm\ngrass.frm\n");
        // Single entry at index 0: avoids resolve()'s scroll-blocker special case (baseId 1).
        writeFile(root / "art/misc/misc.lst", "thing.frm\n");

        resources.files().addDataPath(root.string());
    }

    ~FixtureResources() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

uint32_t typeByte(uint32_t fid) {
    return fid >> 24;
}

uint32_t baseId(uint32_t fid) {
    return fid & 0x00FFFFFF;
}

} // namespace

// The regression guard: the FID type byte must be the FRM_TYPE / engine OBJ_TYPE
// ordinal. WALL is 3, not 4 (the old frmTypeMap encoded 4/5/6/7/8, which the
// engine reads as the next type up).
TEST_CASE("resolveFid encodes the engine FRM type byte", "[resource][frm]") {
    FixtureResources fx;
    auto& resolver = fx.resources.frmResolver();

    const auto wall = resolver.resolveFid("art/walls/wall2.frm");
    REQUIRE(wall.has_value());
    CHECK(typeByte(*wall) == static_cast<uint32_t>(Frm::FRM_TYPE::WALL)); // 3, not 4
    CHECK(baseId(*wall) == 1);                                            // wall2 is index 1

    const auto tile = resolver.resolveFid("art/tiles/grass.frm");
    REQUIRE(tile.has_value());
    CHECK(typeByte(*tile) == static_cast<uint32_t>(Frm::FRM_TYPE::TILE)); // 4, not 5

    const auto misc = resolver.resolveFid("art/misc/thing.frm");
    REQUIRE(misc.has_value());
    CHECK(typeByte(*misc) == static_cast<uint32_t>(Frm::FRM_TYPE::MISC)); // 5, not 6

    const auto item = resolver.resolveFid("art/items/rock.frm");
    REQUIRE(item.has_value());
    CHECK(typeByte(*item) == static_cast<uint32_t>(Frm::FRM_TYPE::ITEM)); // 0 (unaffected)
    CHECK(baseId(*item) == 1);
}

// resolveFid is the inverse of resolve() for non-critter types.
TEST_CASE("resolveFid round-trips through resolve for non-critters", "[resource][frm]") {
    FixtureResources fx;
    auto& resolver = fx.resources.frmResolver();

    for (const auto& path : {
             std::string("art/items/knife.frm"),
             std::string("art/walls/wall1.frm"),
             std::string("art/walls/wall3.frm"),
             std::string("art/tiles/floor.frm"),
             std::string("art/scenery/tree.frm"),
             std::string("art/misc/thing.frm"),
         }) {
        const auto fid = resolver.resolveFid(path);
        REQUIRE(fid.has_value());
        CHECK(resolver.resolve(*fid) == path);
    }
}

// Leniency matches the engine LST lookup: a leading slash and backslashes are
// normalized, and the filename is matched case-insensitively. The directory
// prefix is matched in canonical lower case (paths in the game data are lower).
TEST_CASE("resolveFid tolerates leading slash, backslashes, and filename case", "[resource][frm]") {
    FixtureResources fx;
    auto& resolver = fx.resources.frmResolver();

    CHECK(resolver.resolveFid("/art\\walls\\WALL1.FRM").has_value());
    CHECK(resolver.resolveFid("art/walls/WALL1.FRM") == resolver.resolveFid("art/walls/wall1.frm"));
}

TEST_CASE("resolveFid returns nullopt without a heuristic fallback", "[resource][frm]") {
    FixtureResources fx;
    auto& resolver = fx.resources.frmResolver();

    CHECK_FALSE(resolver.resolveFid("art/walls/missing.frm").has_value()); // under art/ but not in LST
    CHECK_FALSE(resolver.resolveFid("scripts/foo.int").has_value());       // outside any art directory
    CHECK_FALSE(resolver.resolveFid("art/walls/wall1.txt").has_value());   // not an FRM extension
    CHECK_FALSE(resolver.resolveFid("").has_value());
}

// Critters carry the engine critter type byte (1); their FID is animation-encoded
// and lossy, so only the type byte is asserted (not a resolve() round-trip).
TEST_CASE("resolveFid resolves critters by base name", "[resource][frm]") {
    FixtureResources fx;
    auto& resolver = fx.resources.frmResolver();

    const auto fid = resolver.resolveFid("art/critters/hmwarraa.frm");
    REQUIRE(fid.has_value());
    CHECK(typeByte(*fid) == static_cast<uint32_t>(Frm::FRM_TYPE::CRITTER)); // 1
}

TEST_CASE("hasFrmExtension recognizes .frm and directional .fr0-.fr5", "[resource][frm]") {
    using geck::resource::hasFrmExtension;

    // The standard single-file FRM and the split critter directional frames.
    CHECK(hasFrmExtension("art/critters/hmwarraa.frm"));
    CHECK(hasFrmExtension("HMWARRAA.FRM")); // case-insensitive
    for (const char* ext : { ".fr0", ".fr1", ".fr2", ".fr3", ".fr4", ".fr5" }) {
        CHECK(hasFrmExtension(std::string("art/critters/hmwarr") + ext));
    }

    // Not FRM: a prefix match would wrongly accept ".frm1"; .fr6 and other
    // formats must be rejected.
    CHECK_FALSE(hasFrmExtension("art/x.frm1"));
    CHECK_FALSE(hasFrmExtension("art/x.fr6"));
    CHECK_FALSE(hasFrmExtension("art/tiles/x.png"));
    CHECK_FALSE(hasFrmExtension("art/x.lst"));
    CHECK_FALSE(hasFrmExtension("frm")); // too short, no dot
}
