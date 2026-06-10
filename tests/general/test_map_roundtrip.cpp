#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "writer/map/MapWriter.h"
#include "support/MapObjectBuilder.h"
#include "support/ProStubProvider.h"
#include "support/TempFile.h"

using namespace geck;
using namespace geck::test;

// MapWriter/MapReader symmetry guard. Builds one object of every type and item/
// scenery subtype that has a distinct serialization path (plus a nested inventory
// item), writes it, reads it back, and asserts the base fields AND every
// type-specific field survive. Needs no .map/.pro fixture - a stub PRO provider
// supplies the subtypes the (de)serialization dereferences.
TEST_CASE("MAP round-trip preserves all object types and inventory", "[map][roundtrip]") {
    StubProvider provider;
    const uint32_t containerPid = pidOf(Pro::OBJECT_TYPE::ITEM, 200);
    const uint32_t armorPid = pidOf(Pro::OBJECT_TYPE::ITEM, 201);
    const uint32_t weaponPid = pidOf(Pro::OBJECT_TYPE::ITEM, 202);
    const uint32_t ammoPid = pidOf(Pro::OBJECT_TYPE::ITEM, 203);
    const uint32_t keyPid = pidOf(Pro::OBJECT_TYPE::ITEM, 204);
    const uint32_t doorPid = pidOf(Pro::OBJECT_TYPE::SCENERY, 300);
    const uint32_t stairsPid = pidOf(Pro::OBJECT_TYPE::SCENERY, 301);
    provider.addItem(containerPid, Pro::ITEM_TYPE::CONTAINER);
    provider.addItem(armorPid, Pro::ITEM_TYPE::ARMOR);
    provider.addItem(weaponPid, Pro::ITEM_TYPE::WEAPON);
    provider.addItem(ammoPid, Pro::ITEM_TYPE::AMMO);
    provider.addItem(keyPid, Pro::ITEM_TYPE::KEY);
    provider.addScenery(doorPid, Pro::SCENERY_TYPE::DOOR);
    provider.addScenery(stairsPid, Pro::SCENERY_TYPE::STAIRS);

    auto original = Map::createEmptyMapFile();
    auto& objects = original.map_objects[0];

    auto add = [&](uint32_t proPid, int32_t seed) {
        auto o = std::make_shared<MapObject>();
        fillBase(*o, seed);
        o->pro_pid = proPid;
        o->elevation = 0;
        objects.push_back(o);
        return o;
    };

    auto wall = add(pidOf(Pro::OBJECT_TYPE::WALL, 100), 1);

    auto critter = add(pidOf(Pro::OBJECT_TYPE::CRITTER, 50), 2);
    critter->player_reaction = 301;
    critter->current_mp = 302;
    critter->combat_results = 303;
    critter->dmg_last_turn = 304;
    critter->ai_packet = 305;
    critter->group_id = 306;
    critter->who_hit_me = 307;
    critter->current_hp = 308;
    critter->current_rad = 309;
    critter->current_poison = 310;

    auto container = add(containerPid, 3);
    container->objects_in_inventory = 1;
    auto armor = std::make_unique<MapObject>();
    fillBase(*armor, 30);
    armor->pro_pid = armorPid;
    armor->elevation = 0;
    armor->amount = 5;
    container->inventory.push_back(std::move(armor));

    auto weapon = add(weaponPid, 4);
    weapon->ammo = 401;
    weapon->ammo_pid = 402;

    auto ammo = add(ammoPid, 5);
    ammo->ammo = 501;

    auto key = add(keyPid, 6);
    key->keycode = 601;

    auto door = add(doorPid, 7);
    door->walkthrough = 701;

    auto stairs = add(stairsPid, 8);
    stairs->elevhex = 801;
    stairs->map = 802;

    auto exitGrid = add(pidOf(Pro::OBJECT_TYPE::MISC, 16), 9);
    exitGrid->exit_map = 901;
    exitGrid->exit_position = 902;
    exitGrid->exit_elevation = 903;
    exitGrid->exit_orientation = 904;

    const size_t objectCount = objects.size();

    TempFile mapFile{ "geck_map_roundtrip_alltypes", ".map" };
    const auto& path = mapFile.path();
    {
        MapWriter writer{ [&](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(path);
        REQUIRE(writer.write(original));
    } // destructor flushes and closes before we read it back

    MapReader reader{ [&](uint32_t pid) { return provider.load(pid); } };
    auto reloaded = reader.openFile(path);
    REQUIRE(reloaded != nullptr);
    const auto& result = reloaded->getMapFile();

    // Header. The filename is padded to FILENAME_LENGTH on write and read back
    // with the padding, so compare only the meaningful prefix.
    CHECK(result.header.version == original.header.version);
    CHECK(result.header.filename.substr(0, original.header.filename.size()) == original.header.filename);
    CHECK(result.header.flags == original.header.flags);
    CHECK(result.header.script_id == original.header.script_id);

    // All objects on elevation 0; the other two elevations stay empty.
    REQUIRE(result.map_objects.at(0).size() == objectCount);
    CHECK(result.map_objects.at(1).empty());
    CHECK(result.map_objects.at(2).empty());

    const auto& got = result.map_objects.at(0);

    // Base fields survive for every object type.
    for (size_t i = 0; i < objectCount; ++i) {
        checkBase(*got[i], *objects[i]);
    }

    // Type-specific fields survive.
    const auto& gotCritter = *got[1];
    CHECK(gotCritter.player_reaction == critter->player_reaction);
    CHECK(gotCritter.current_mp == critter->current_mp);
    CHECK(gotCritter.combat_results == critter->combat_results);
    CHECK(gotCritter.dmg_last_turn == critter->dmg_last_turn);
    CHECK(gotCritter.ai_packet == critter->ai_packet);
    CHECK(gotCritter.group_id == critter->group_id);
    CHECK(gotCritter.who_hit_me == critter->who_hit_me);
    CHECK(gotCritter.current_hp == critter->current_hp);
    CHECK(gotCritter.current_rad == critter->current_rad);
    CHECK(gotCritter.current_poison == critter->current_poison);

    const auto& gotContainer = *got[2];
    CHECK(gotContainer.objects_in_inventory == 1);
    REQUIRE(gotContainer.inventory.size() == 1);
    checkBase(*gotContainer.inventory[0], *container->inventory[0]);
    CHECK(gotContainer.inventory[0]->amount == 5);

    const auto& gotWeapon = *got[3];
    CHECK(gotWeapon.ammo == weapon->ammo);
    CHECK(gotWeapon.ammo_pid == weapon->ammo_pid);

    CHECK(got[4]->ammo == ammo->ammo);
    CHECK(got[5]->keycode == key->keycode);
    CHECK(got[6]->walkthrough == door->walkthrough);

    const auto& gotStairs = *got[7];
    CHECK(gotStairs.elevhex == stairs->elevhex);
    CHECK(gotStairs.map == stairs->map);

    const auto& gotExit = *got[8];
    CHECK(gotExit.exit_map == exitGrid->exit_map);
    CHECK(gotExit.exit_position == exitGrid->exit_position);
    CHECK(gotExit.exit_elevation == exitGrid->exit_elevation);
    CHECK(gotExit.exit_orientation == exitGrid->exit_orientation);
}

// MapObject::cloneDeep underpins the inventory and copy-elevation undo commands,
// so its deep-copy semantics (including nested inventory) must hold: the clone
// matches field-for-field, and mutating the clone never touches the original.
TEST_CASE("MapObject::cloneDeep deep-copies fields and inventory", "[map][clone]") {
    MapObject original;
    fillBase(original, 7);
    original.pro_pid = pidOf(Pro::OBJECT_TYPE::ITEM, 200);
    original.elevation = 2;
    original.objects_in_inventory = 1;

    auto child = std::make_unique<MapObject>();
    fillBase(*child, 8);
    child->pro_pid = pidOf(Pro::OBJECT_TYPE::ITEM, 201);
    child->elevation = 2;
    child->amount = 9;
    original.inventory.push_back(std::move(child));

    auto clone = original.cloneDeep();
    REQUIRE(clone != nullptr);
    checkBase(*clone, original);
    REQUIRE(clone->inventory.size() == 1);
    checkBase(*clone->inventory[0], *original.inventory[0]);
    CHECK(clone->inventory[0]->amount == 9);

    // The clone is independent: editing it must not affect the original.
    CHECK(clone->inventory[0].get() != original.inventory[0].get());
    clone->inventory[0]->amount = 999;
    clone->elevation = 0;
    CHECK(original.inventory[0]->amount == 9);
    CHECK(original.elevation == 2);
}

// A map with only elevation 0 enabled (flags disable elevations 1 and 2) must
// still round-trip. The engine frames the object section as exactly three
// per-elevation count blocks regardless of which elevations are enabled
// (objectSaveAll / objectLoadAll); before the fix the reader consumed only one
// count block here and desynced the stream. Walls/critters don't dereference a
// Pro, so a provider that returns nullptr is sufficient.
TEST_CASE("MAP single-enabled-elevation map keeps engine 3-block object framing", "[map][roundtrip][compat]") {
    StubProvider provider;

    auto original = Map::createEmptyMapFile();
    // Disable elevations 1 and 2 (flag bits 0x4 and 0x8 set => tiles absent).
    original.header.flags = 0x4 | 0x8;
    original.tiles.erase(1);
    original.tiles.erase(2);

    auto& objects = original.map_objects[0];
    auto add = [&](uint32_t proPid, int32_t seed) {
        auto o = std::make_shared<MapObject>();
        fillBase(*o, seed);
        o->pro_pid = proPid;
        o->elevation = 0;
        objects.push_back(o);
        return o;
    };
    add(pidOf(Pro::OBJECT_TYPE::WALL, 100), 1);
    auto critter = add(pidOf(Pro::OBJECT_TYPE::CRITTER, 50), 2);
    critter->current_hp = 42;

    const size_t objectCount = objects.size();

    TempFile mapFile{ "geck_map_roundtrip_single_elev", ".map" };
    const auto& path = mapFile.path();
    {
        MapWriter writer{ [&](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(path);
        REQUIRE(writer.write(original));
    }

    MapReader reader{ [&](uint32_t pid) { return provider.load(pid); } };
    auto reloaded = reader.openFile(path);
    REQUIRE(reloaded != nullptr);
    const auto& result = reloaded->getMapFile();

    CHECK(result.header.flags == (0x4u | 0x8u));

    // All three object blocks are present; only elevation 0 holds objects.
    REQUIRE(result.map_objects.at(0).size() == objectCount);
    CHECK(result.map_objects.at(1).empty());
    CHECK(result.map_objects.at(2).empty());
    checkBase(*result.map_objects.at(0)[0], *objects[0]);
    CHECK(result.map_objects.at(0)[1]->current_hp == 42);

    // Only elevation 0's tiles are present on disk.
    CHECK(result.tiles.count(0) == 1);
    CHECK(result.tiles.count(1) == 0);
    CHECK(result.tiles.count(2) == 0);
}

// A non-exit-grid MISC object must not serialize the 4 trailing exit fields:
// the engine writes them only for exit-grid PIDs (MISC ids 16-23). Two
// otherwise-identical single-object maps - one exit-grid MISC, one ordinary
// MISC - must differ in size by exactly those 4 int32 fields (16 bytes). The
// ordinary MISC must also still round-trip without desyncing.
TEST_CASE("MAP non-exit MISC object writes no trailing exit data", "[map][roundtrip][compat]") {
    StubProvider provider;

    auto buildSingleMisc = [&](uint32_t miscIndex) {
        auto map = Map::createEmptyMapFile();
        auto o = std::make_shared<MapObject>();
        fillBase(*o, 1);
        o->pro_pid = pidOf(Pro::OBJECT_TYPE::MISC, miscIndex);
        o->elevation = 0;
        o->exit_map = 901;
        o->exit_position = 902;
        o->exit_elevation = 903;
        o->exit_orientation = 904;
        map.map_objects[0].push_back(o);
        return map;
    };

    auto writeMap = [&](const Map::MapFile& m, const std::filesystem::path& p) {
        MapWriter writer{ [&](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(p);
        REQUIRE(writer.write(m));
    };

    auto readMap = [&](const std::filesystem::path& p) {
        MapReader reader{ [&](uint32_t pid) { return provider.load(pid); } };
        return reader.openFile(p);
    };

    TempFile exitFile{ "geck_misc_exit", ".map" };
    TempFile plainFile{ "geck_misc_plain", ".map" };
    const auto& exitPath = exitFile.path();
    const auto& plainPath = plainFile.path();

    writeMap(buildSingleMisc(16), exitPath); // exit grid -> 4 trailing fields
    writeMap(buildSingleMisc(5), plainPath); // ordinary MISC -> none

    const auto exitSize = std::filesystem::file_size(exitPath);
    const auto plainSize = std::filesystem::file_size(plainPath);
    CHECK(exitSize - plainSize == 4 * sizeof(uint32_t));

    // The ordinary MISC still round-trips; its (unserialized) exit fields come
    // back at their defaults rather than the sentinels we set.
    auto reloadedPlain = readMap(plainPath);
    REQUIRE(reloadedPlain != nullptr);
    const auto& plain = reloadedPlain->getMapFile();
    REQUIRE(plain.map_objects.at(0).size() == 1);
    const auto& misc = *plain.map_objects.at(0)[0];
    CHECK(misc.exit_map == 0);
    CHECK(misc.exit_position == 0);
    CHECK(misc.exit_elevation == 0);
    CHECK(misc.exit_orientation == 0);

    // The exit grid preserves them.
    auto reloadedExit = readMap(exitPath);
    REQUIRE(reloadedExit != nullptr);
    const auto& exitMap = reloadedExit->getMapFile();
    REQUIRE(exitMap.map_objects.at(0).size() == 1);
    const auto& exitObj = *exitMap.map_objects.at(0)[0];
    CHECK(exitObj.exit_map == 901);
    CHECK(exitObj.exit_position == 902);
    CHECK(exitObj.exit_elevation == 903);
    CHECK(exitObj.exit_orientation == 904);
}
