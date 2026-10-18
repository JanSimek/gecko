#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "format/frm/Frm.h"
#include "format/frm/FrmId.h"
#include "resource/FrmResolver.h"
#include "resource/GameResources.h"

#include "support/ArtLstTree.h"
#include "support/Fixtures.h"

namespace fs = std::filesystem;
using geck::Frm;
using geck::resource::GameResources;

// The regression guard: the FID type must be the ObjectType / engine OBJ_TYPE
// ordinal. WALL is 3, not 4 (the old frmTypeMap encoded 4/5/6/7/8, which the
// engine reads as the next type up).
TEST_CASE("resolveFid encodes the engine FRM type byte", "[resource][frm]") {
    geck::test::ArtLstTree fx{ "geck_frmresolver_test" };
    auto& resolver = fx.mount().frmResolver();

    using geck::FrmId;
    using geck::ObjectType;

    // The whole FID, so the type nibble, the frame id and the zero rotation/animation bits all count.
    const auto wall = resolver.resolveFid("art/walls/wall2.frm");
    REQUIRE(wall.has_value());
    CHECK(FrmId(*wall).objectType() == ObjectType::Wall); // 3, not 4
    CHECK(*wall == FrmId(ObjectType::Wall, 1).fid());     // wall2 is index 1

    const auto tile = resolver.resolveFid("art/tiles/grass.frm");
    REQUIRE(tile.has_value());
    CHECK(FrmId(*tile).objectType() == ObjectType::Tile); // 4, not 5
    CHECK(*tile == FrmId(ObjectType::Tile, 1).fid());

    const auto misc = resolver.resolveFid("art/misc/thing.frm");
    REQUIRE(misc.has_value());
    CHECK(FrmId(*misc).objectType() == ObjectType::Misc); // 5, not 6
    CHECK(*misc == FrmId(ObjectType::Misc, 0).fid());

    const auto item = resolver.resolveFid("art/items/rock.frm");
    REQUIRE(item.has_value());
    CHECK(FrmId(*item).objectType() == ObjectType::Item); // 0 (unaffected)
    CHECK(*item == FrmId(ObjectType::Item, 1).fid());
}

// resolveFid is the inverse of resolve() for non-critter types.
TEST_CASE("resolveFid round-trips through resolve for non-critters", "[resource][frm]") {
    geck::test::ArtLstTree fx{ "geck_frmresolver_test" };
    auto& resolver = fx.mount().frmResolver();

    for (const auto& path : {
             std::string("art/items/knife.frm"),
             std::string("art/walls/wall1.frm"),
             std::string("art/walls/wall3.frm"),
             std::string("art/tiles/floor.frm"),
             std::string("art/scenery/tree1.frm"),
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
    geck::test::ArtLstTree fx{ "geck_frmresolver_test" };
    auto& resolver = fx.mount().frmResolver();

    CHECK(resolver.resolveFid("/art\\walls\\WALL1.FRM").has_value());
    CHECK(resolver.resolveFid("art/walls/WALL1.FRM") == resolver.resolveFid("art/walls/wall1.frm"));
}

TEST_CASE("resolveFid returns nullopt without a heuristic fallback", "[resource][frm]") {
    geck::test::ArtLstTree fx{ "geck_frmresolver_test" };
    auto& resolver = fx.mount().frmResolver();

    CHECK_FALSE(resolver.resolveFid("art/walls/missing.frm").has_value()); // under art/ but not in LST
    CHECK_FALSE(resolver.resolveFid("scripts/foo.int").has_value());       // outside any art directory
    CHECK_FALSE(resolver.resolveFid("art/walls/wall1.txt").has_value());   // not an FRM extension
    CHECK_FALSE(resolver.resolveFid("").has_value());
}

// Critters carry the engine critter type (1); their FID is animation-encoded
// and lossy, so only the type is asserted (not a resolve() round-trip).
TEST_CASE("resolveFid resolves critters by base name", "[resource][frm]") {
    geck::test::ArtLstTree fx{ "geck_frmresolver_test" };
    auto& resolver = fx.mount().frmResolver();

    const auto fid = resolver.resolveFid("art/critters/hmwarraa.frm");
    REQUIRE(fid.has_value());
    CHECK(geck::FrmId(*fid).objectType() == geck::ObjectType::Critter); // 1
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

// Wall art 620 is an ordinary wall: shipped walls.lst[620] is rl11000.frm, the "Steel Railing"
// (wall proto 171) placed in ncr1, sftanker and a dozen other maps. resolve() used to swap it for the
// editor's wallblock.frm, mistaking the index of wall proto 620 (itself just a wall, nec128.frm) for
// art of a "wall blocker".
TEST_CASE("resolve draws wall art 620 from walls.lst like any other wall", "[resource][frm]") {
    geck::test::DataTree tree{ "geck_frmresolver_wall620_test" };
    std::string walls;
    for (int i = 0; i < 622; ++i) {
        walls += (i == 620 ? std::string("rl11000.frm") : "wall" + std::to_string(i) + ".frm") + "\n";
    }
    tree.write("art/walls/walls.lst", walls);
    auto& resolver = tree.mount().frmResolver();

    CHECK(resolver.resolve(0x0300026Cu) == "art/walls/rl11000.frm");
    CHECK(resolver.resolveFid("art/walls/rl11000.frm") == 0x0300026Cu);
}

TEST_CASE("resolve picks a critter's art from the FID's weapon and animation codes", "[resource][frm]") {
    geck::test::ArtLstTree fixture{ "geck_frmresolver_test" };
    // Any parseable FRM stands in for the art; resolve only needs the file to be there.
    const auto art = fixture.root() / "art/critters";
    fs::copy_file(geck::test::dataPath("flower.frm"), art / "hmwarrda.frm"); // standing, holding a knife
    fs::copy_file(geck::test::dataPath("flower.frm"), art / "hmwarrra.frm"); // dead, single frame
    fixture.mount().files().refresh();                                       // the mount lists what existed when it was made
    auto& resolver = fixture.mount().frmResolver();
    const auto critter = [](uint32_t animation, uint32_t weapon) {
        return geck::FrmId(geck::ObjectType::Critter, 0, animation, weapon, geck::Rotation::NE).fid();
    };

    // The name stops at the comma, as the engine reads critters.lst.
    CHECK(resolver.resolve(critter(0, 0)) == "art/critters/hmwarraa.frm");
    CHECK(resolver.resolve(critter(0, 1)) == "art/critters/hmwarrda.frm");
    CHECK(resolver.resolve(critter(48, 0)) == "art/critters/hmwarrra.frm"); // ANIM_FALL_BACK_SF
    // No club art in the data: the standing art keeps the critter visible.
    CHECK(resolver.resolve(critter(0, 2)) == "art/critters/hmwarraa.frm");
}
