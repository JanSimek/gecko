#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "format/map/MapObject.h"
#include "format/pro/ProtoTypes.h"

#include "support/MapObjectBuilder.h"

// A default-constructed MapObject must have deterministic field values: the
// "none" sentinels (-1) for the script/critter ids that the Fallout 2 engine
// uses, amount == 1, and zero everywhere else. This guards against the
// uninitialized-field reads that previously occurred when an object-creation
// path (e.g. inventory items) left fields unset.

using geck::MapObject;

TEST_CASE("MapObject default field values are deterministic", "[map_object]") {
    MapObject obj;

    SECTION("engine 'none' sentinels are -1") {
        CHECK(obj.critter_index == -1);
        CHECK(obj.map_scripts_pid == -1);
        CHECK(obj.script_id == -1);
    }

    SECTION("amount defaults to 1") {
        CHECK(obj.amount == 1u);
    }

    SECTION("all other fields are zero-initialised") {
        CHECK(obj.unknown0 == 0u);
        CHECK(obj.position == 0);
        CHECK(obj.x == 0u);
        CHECK(obj.y == 0u);
        CHECK(obj.sx == 0);
        CHECK(obj.sy == 0);
        CHECK(obj.frame_number == 0u);
        CHECK(obj.direction == 0u);
        CHECK(obj.frm_pid == 0u);
        CHECK(obj.flags == 0u);
        CHECK(obj.elevation == 0u);
        CHECK(obj.pro_pid == 0u);
        CHECK(obj.light_radius == 0u);
        CHECK(obj.light_intensity == 0u);
        CHECK(obj.outline_color == 0u);
        CHECK(obj.objects_in_inventory == 0u);
        CHECK(obj.max_inventory_size == 0u);
        CHECK(obj.ammo == 0u);
        CHECK(obj.keycode == 0u);
        CHECK(obj.ammo_pid == 0u);
        CHECK(obj.exit_map == 0u);
        CHECK(obj.exit_position == 0u);
        CHECK(obj.exit_elevation == 0u);
        CHECK(obj.exit_orientation == 0u);
        CHECK(obj.inventory.empty());
    }
}

// MapObject::cloneDeep underpins the inventory and copy-elevation undo commands,
// so its deep-copy semantics (including nested inventory) must hold: the clone
// matches field-for-field, and mutating the clone never touches the original.
TEST_CASE("MapObject::cloneDeep deep-copies fields and inventory", "[map_object]") {
    MapObject original;
    geck::test::fillBase(original, 7);
    original.pro_pid = geck::ProtoId(geck::ObjectType::Item, 200).pid();
    original.elevation = 2;
    original.objects_in_inventory = 1;

    auto child = std::make_unique<MapObject>();
    geck::test::fillBase(*child, 8);
    child->pro_pid = geck::ProtoId(geck::ObjectType::Item, 201).pid();
    child->elevation = 2;
    child->amount = 9;
    original.inventory.push_back(std::move(child));

    auto clone = original.cloneDeep();
    REQUIRE(clone != nullptr);
    geck::test::checkBase(*clone, original);
    REQUIRE(clone->inventory.size() == 1);
    geck::test::checkBase(*clone->inventory[0], *original.inventory[0]);
    CHECK(clone->inventory[0]->amount == 9);

    // The clone is independent: editing it must not affect the original.
    CHECK(clone->inventory[0].get() != original.inventory[0].get());
    clone->inventory[0]->amount = 999;
    clone->elevation = 0;
    CHECK(original.inventory[0]->amount == 9);
    CHECK(original.elevation == 2);
}
