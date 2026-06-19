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

    // Scroll blockers are identified by FRM base id == 1 (scrblk.frm).
    MapObject blocker;
    blocker.pro_pid = typePid(Pro::OBJECT_TYPE::MISC, 620);
    blocker.frm_pid = 1;

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
