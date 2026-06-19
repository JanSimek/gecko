#include <catch2/catch_test_macros.hpp>

#include "format/map/MapObject.h"
#include "format/map/MapObjectViews.h"

using geck::CritterInstance;
using geck::ExitGridInstance;
using geck::MapObject;
using geck::SceneryInstance;
using geck::WeaponInstance;

// The views are a typed lens over MapObject's flat storage: a setter must write
// the underlying raw field, and a getter must read it back unchanged.

TEST_CASE("ExitGridInstance maps to the raw exit_* fields", "[mapobject][views]") {
    MapObject object;
    ExitGridInstance view{ object };

    view.setDestinationMap(42);
    view.setDestinationPosition(1234);
    view.setDestinationElevation(2);
    view.setOrientation(3);

    CHECK(object.exit_map == 42);
    CHECK(object.exit_position == 1234);
    CHECK(object.exit_elevation == 2);
    CHECK(object.exit_orientation == 3);

    object.exit_map = 7;
    CHECK(view.destinationMap() == 7);
    CHECK(view.destinationPosition() == 1234);
}

TEST_CASE("CritterInstance maps to the per-instance critter fields", "[mapobject][views]") {
    MapObject object;
    CritterInstance view{ object };

    view.setAiPacket(11);
    view.setGroupId(1);
    view.setCurrentHp(55);
    view.setCurrentRad(6);
    view.setCurrentPoison(7);

    CHECK(object.ai_packet == 11);
    CHECK(object.group_id == 1);
    CHECK(object.current_hp == 55);
    CHECK(object.current_rad == 6);
    CHECK(object.current_poison == 7);

    object.current_hp = 99;
    CHECK(view.currentHp() == 99);
}

TEST_CASE("WeaponInstance maps to the ammo fields", "[mapobject][views]") {
    MapObject object;
    WeaponInstance view{ object };

    view.setAmmoCount(24);
    view.setAmmoPid(0x00000005);

    CHECK(object.ammo == 24);
    CHECK(object.ammo_pid == 0x00000005);

    object.ammo = 0;
    CHECK(view.ammoCount() == 0);
}

TEST_CASE("SceneryInstance maps to door/elevator/stairs fields", "[mapobject][views]") {
    MapObject object;
    SceneryInstance view{ object };

    view.setWalkThrough(1);
    view.setElevationType(2);
    view.setElevationLevel(3);
    view.setDestinationElevhex(4567);
    view.setDestinationMap(8);

    CHECK(object.walkthrough == 1);
    CHECK(object.elevtype == 2);
    CHECK(object.elevlevel == 3);
    CHECK(object.elevhex == 4567);
    CHECK(object.map == 8);
}
