#include <catch2/catch_test_macros.hpp>

#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "ui/core/VisibilitySettings.h"
#include "rendering/ObjectVisibility.h"
#include "util/Constants.h"

using namespace geck;

namespace {

uint32_t typePid(Pro::OBJECT_TYPE type, uint32_t baseId) {
    return (static_cast<uint32_t>(type) << FileFormat::TYPE_MASK_SHIFT) | baseId;
}

} // namespace

// Selection routes through isObjectVisible (the same rule renderObjects draws by), so a click
// can never select an object the user can't see. Regression guard for selecting a scroll blocker
// while its layer is hidden, which produced an invisible selection.
TEST_CASE("isObjectVisible follows the layer toggles that decide what is drawn", "[visibility]") {
    // A plain item: neither a wall (PRO type) nor a scroll blocker (FRM base id 1).
    MapObject regular;
    regular.pro_pid = typePid(Pro::OBJECT_TYPE::ITEM, 100);
    regular.frm_pid = 100;

    MapObject wall;
    wall.pro_pid = typePid(Pro::OBJECT_TYPE::WALL, 5);
    wall.frm_pid = 5;

    MapObject critter;
    critter.pro_pid = typePid(Pro::OBJECT_TYPE::CRITTER, 50);
    critter.frm_pid = 50;

    // A scroll blocker is proto 0x0500000C and nothing else — the exact pid the engine matches in
    // _obj_scroll_blocking_at(). Its art is art/misc/scrblk.frm; see the ammo case below for why
    // the art cannot be the test.
    MapObject blocker;
    blocker.pro_pid = 0x0500000C;
    blocker.frm_pid = 0x05000001;

    REQUIRE_FALSE(regular.isWallObject());
    REQUIRE(wall.isWallObject());
    REQUIRE(blocker.isScrollBlocker());

    VisibilitySettings vis;
    vis.showObjects = true;
    vis.showCritters = true;
    vis.showWalls = true;
    vis.showScrollBlockers = true;

    SECTION("all layers on -> everything is selectable") {
        CHECK(isObjectVisible(regular, vis));
        CHECK(isObjectVisible(wall, vis));
        CHECK(isObjectVisible(critter, vis));
        CHECK(isObjectVisible(blocker, vis));
    }

    SECTION("each toggle controls only its own category (not a master switch)") {
        // showObjects covers only generic objects (items/scenery/misc), not walls/critters/blockers.
        vis.showObjects = false;
        CHECK_FALSE(isObjectVisible(regular, vis));
        CHECK(isObjectVisible(wall, vis));
        CHECK(isObjectVisible(critter, vis));
        CHECK(isObjectVisible(blocker, vis));
    }

    SECTION("hiding critters hides only critters") {
        vis.showCritters = false;
        CHECK(isObjectVisible(regular, vis));
        CHECK(isObjectVisible(wall, vis));
        CHECK_FALSE(isObjectVisible(critter, vis));
        CHECK(isObjectVisible(blocker, vis));
    }

    SECTION("hiding walls hides only walls") {
        vis.showWalls = false;
        CHECK(isObjectVisible(regular, vis));
        CHECK_FALSE(isObjectVisible(wall, vis));
        CHECK(isObjectVisible(critter, vis));
        CHECK(isObjectVisible(blocker, vis));
    }

    SECTION("hiding scroll blockers hides only scroll blockers (the reported bug)") {
        vis.showScrollBlockers = false;
        CHECK(isObjectVisible(regular, vis));
        CHECK(isObjectVisible(wall, vis));
        CHECK(isObjectVisible(critter, vis));
        CHECK_FALSE(isObjectVisible(blocker, vis));
    }
}

TEST_CASE("A scroll blocker is the engine's proto, not whatever uses scrblk art", "[rendering][visibility]") {
    // The engine's rule, from _obj_scroll_blocking_at(): `obj->pid == 0x500000C`, art irrelevant.
    // Testing the FRM's low 24 bits instead made every object drawn with art index 1 a blocker -
    // art/items/ammo.frm is FID 0x00000001 - so ammo boxes vanished from the editor while loading,
    // saving and rendering in game perfectly well.
    MapObject ammo;
    ammo.pro_pid = typePid(Pro::OBJECT_TYPE::ITEM, 29);
    ammo.frm_pid = 0x00000001; // art/items/ammo.frm

    MapObject blocker;
    blocker.pro_pid = WallBlockers::SCROLL_BLOCKER_PID;
    blocker.frm_pid = WallBlockers::SCROLL_BLOCKER_FRM_PID;

    CHECK(blocker.pro_pid == 0x0500000Cu); // the literal the engine compares against
    CHECK_FALSE(ammo.isScrollBlocker());
    CHECK(blocker.isScrollBlocker());

    // Every other art type that collides on index 1 in shipped maps.
    for (const uint32_t artType : { 0u, 1u, 2u, 3u, 4u }) {
        MapObject other;
        other.pro_pid = typePid(Pro::OBJECT_TYPE::SCENERY, 7);
        other.frm_pid = (artType << 24) | 1u;
        CHECK_FALSE(other.isScrollBlocker());
    }

    // Proto 24 is "Flare": drawing it with blocker art does not make it one, and the editor must
    // not write it for the Scroll Blocker Rectangle tool.
    MapObject flare;
    flare.pro_pid = 0x05000000 | WallBlockers::GENERIC_PROTO_ID;
    flare.frm_pid = WallBlockers::SCROLL_BLOCKER_FRM_PID;
    CHECK_FALSE(flare.isScrollBlocker());

    VisibilitySettings vis;
    vis.showObjects = true;
    vis.showCritters = true;
    vis.showWalls = true;
    vis.showScrollBlockers = false;

    CHECK(isObjectVisible(ammo, vis)); // stays drawn with the blocker layer off
    CHECK_FALSE(isObjectVisible(blocker, vis));
}
