#include <catch2/catch_test_macros.hpp>

#include "format/map/MapObject.h"

// A default-constructed MapObject must have deterministic field values: the
// "none" sentinels (-1) for the script/critter ids that the Fallout 2 engine
// uses, amount == 1, and zero everywhere else. This guards against the
// uninitialized-field reads that previously occurred when an object-creation
// path (e.g. inventory items) left fields unset.

using geck::MapObject;

TEST_CASE("MapObject default field values are deterministic", "[mapobject]")
{
    MapObject obj;

    SECTION("engine 'none' sentinels are -1")
    {
        REQUIRE(obj.critter_index == -1);
        REQUIRE(obj.map_scripts_pid == -1);
        REQUIRE(obj.script_id == -1);
    }

    SECTION("amount defaults to 1")
    {
        REQUIRE(obj.amount == 1u);
    }

    SECTION("all other fields are zero-initialised")
    {
        REQUIRE(obj.unknown0 == 0u);
        REQUIRE(obj.position == 0);
        REQUIRE(obj.x == 0u);
        REQUIRE(obj.y == 0u);
        REQUIRE(obj.sx == 0);
        REQUIRE(obj.sy == 0);
        REQUIRE(obj.frame_number == 0u);
        REQUIRE(obj.direction == 0u);
        REQUIRE(obj.frm_pid == 0u);
        REQUIRE(obj.flags == 0u);
        REQUIRE(obj.elevation == 0u);
        REQUIRE(obj.pro_pid == 0u);
        REQUIRE(obj.light_radius == 0u);
        REQUIRE(obj.light_intensity == 0u);
        REQUIRE(obj.outline_color == 0u);
        REQUIRE(obj.objects_in_inventory == 0u);
        REQUIRE(obj.max_inventory_size == 0u);
        REQUIRE(obj.ammo == 0u);
        REQUIRE(obj.keycode == 0u);
        REQUIRE(obj.ammo_pid == 0u);
        REQUIRE(obj.exit_map == 0u);
        REQUIRE(obj.exit_position == 0u);
        REQUIRE(obj.exit_elevation == 0u);
        REQUIRE(obj.exit_orientation == 0u);
        REQUIRE(obj.inventory.empty());
    }
}
