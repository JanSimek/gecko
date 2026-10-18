#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "support/Fixtures.h"
#include "support/MapFileIo.h"
#include "support/MapObjectBuilder.h"

using namespace geck;
using namespace geck::test;

// MapWriter/MapReader symmetry guard. Builds one object of every type and item/
// scenery subtype that has a distinct serialization path (plus a nested inventory
// item), writes it, reads it back, and asserts the base fields AND every
// type-specific field survive. Needs no .map/.pro fixture - a stub PRO provider
// supplies the subtypes the (de)serialization dereferences.
TEST_CASE("MAP round-trip preserves all object types and inventory", "[map][roundtrip]") {
    StubProvider provider;
    const uint32_t containerPid = ProtoId(ObjectType::Item, 200).pid();
    const uint32_t armorPid = ProtoId(ObjectType::Item, 201).pid();
    const uint32_t weaponPid = ProtoId(ObjectType::Item, 202).pid();
    const uint32_t ammoPid = ProtoId(ObjectType::Item, 203).pid();
    const uint32_t keyPid = ProtoId(ObjectType::Item, 204).pid();
    const uint32_t doorPid = ProtoId(ObjectType::Scenery, 300).pid();
    const uint32_t stairsPid = ProtoId(ObjectType::Scenery, 301).pid();
    provider.addItem(containerPid, ItemType::Container);
    provider.addItem(armorPid, ItemType::Armor);
    provider.addItem(weaponPid, ItemType::Weapon);
    provider.addItem(ammoPid, ItemType::Ammo);
    provider.addItem(keyPid, ItemType::Key);
    provider.addScenery(doorPid, SceneryType::Door);
    provider.addScenery(stairsPid, SceneryType::Stairs);

    auto original = Map::createEmptyMapFile();
    const auto& objects = original.map_objects[0];
    const auto add = [&original](uint32_t proPid, int32_t seed) { return addSeeded(original, proPid, seed); };

    auto wall = add(ProtoId(ObjectType::Wall, 100).pid(), 1);

    auto critter = add(ProtoId(ObjectType::Critter, 50).pid(), 2);
    critter->damage_last_turn = 301;
    critter->maneuver = 302;
    critter->current_ap = 303;
    critter->combat_results = 304;
    critter->ai_packet = 305;
    critter->group_id = 306;
    critter->who_hit_me = 307;
    critter->current_hp = 308;
    critter->current_rad = 309;
    critter->current_poison = 310;

    auto container = add(containerPid, 3);
    container->objects_in_inventory = 1;
    auto armor = std::make_unique<MapObject>(std::move(*seededObject(armorPid, 30)));
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

    auto exitGrid = add(ProtoId(ObjectType::Misc, 16).pid(), 9);
    exitGrid->exit_map = 901;
    exitGrid->exit_position = 902;
    exitGrid->exit_elevation = 903;
    exitGrid->exit_orientation = 904;

    const size_t objectCount = objects.size();

    const auto reloaded = readMapFile(mapBytes(original, provider), provider);
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
    CHECK(gotCritter.damage_last_turn == critter->damage_last_turn);
    CHECK(gotCritter.maneuver == critter->maneuver);
    CHECK(gotCritter.current_ap == critter->current_ap);
    CHECK(gotCritter.combat_results == critter->combat_results);
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
    original.pro_pid = ProtoId(ObjectType::Item, 200).pid();
    original.elevation = 2;
    original.objects_in_inventory = 1;

    auto child = std::make_unique<MapObject>();
    fillBase(*child, 8);
    child->pro_pid = ProtoId(ObjectType::Item, 201).pid();
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
    // Disable elevations 1 and 2 (their flag bits set => tiles absent).
    constexpr uint32_t ELEVATIONS_1_AND_2_ABSENT = Map::elevationFlag(1) | Map::elevationFlag(2);
    original.header.flags = ELEVATIONS_1_AND_2_ABSENT;
    original.tiles.erase(1);
    original.tiles.erase(2);

    const auto& objects = original.map_objects[0];
    addSeeded(original, ProtoId(ObjectType::Wall, 100).pid(), 1);
    auto critter = addSeeded(original, ProtoId(ObjectType::Critter, 50).pid(), 2);
    critter->current_hp = 42;

    const size_t objectCount = objects.size();

    const auto reloaded = readMapFile(mapBytes(original, provider), provider);
    const auto& result = reloaded->getMapFile();

    CHECK(result.header.flags == ELEVATIONS_1_AND_2_ABSENT);

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

    auto singleMiscBytes = [&](uint32_t miscIndex) {
        auto map = Map::createEmptyMapFile();
        auto o = addSeeded(map, ProtoId(ObjectType::Misc, miscIndex).pid(), 1);
        o->exit_map = 901;
        o->exit_position = 902;
        o->exit_elevation = 903;
        o->exit_orientation = 904;
        return mapBytes(map, provider);
    };

    const auto exitBytes = singleMiscBytes(16); // exit grid -> 4 trailing fields
    const auto plainBytes = singleMiscBytes(5); // ordinary MISC -> none
    CHECK(exitBytes.size() - plainBytes.size() == 4 * sizeof(uint32_t));

    // The ordinary MISC still round-trips; its (unserialized) exit fields come
    // back at their defaults rather than the sentinels we set.
    const auto reloadedPlain = readMapFile(plainBytes, provider);
    const auto& plain = reloadedPlain->getMapFile();
    REQUIRE(plain.map_objects.at(0).size() == 1);
    const auto& misc = *plain.map_objects.at(0)[0];
    CHECK(misc.exit_map == 0);
    CHECK(misc.exit_position == 0);
    CHECK(misc.exit_elevation == 0);
    CHECK(misc.exit_orientation == 0);

    // The exit grid preserves them.
    const auto reloadedExit = readMapFile(exitBytes, provider);
    const auto& exitMap = reloadedExit->getMapFile();
    REQUIRE(exitMap.map_objects.at(0).size() == 1);
    const auto& exitObj = *exitMap.map_objects.at(0)[0];
    CHECK(exitObj.exit_map == 901);
    CHECK(exitObj.exit_position == 902);
    CHECK(exitObj.exit_elevation == 903);
    CHECK(exitObj.exit_orientation == 904);
}

// An object whose PID names no known type is legal, not corruption: the engine's objectDataRead and
// objectDataWrite (fallout2-ce proto.cc) both fall out of their type switch through `default:`, so
// such a record is the 22-field common block alone, with no type-specific tail. RPU's epamain1.map
// and epamain2.map each carry 17 of them with pid -1, and rejecting them made both maps unreadable.
// Reader and writer have to agree here, or every object after one shifts by the width of a tail that
// was never on disk.
TEST_CASE("MAP round-trip keeps an object whose PID has no known type", "[map][roundtrip]") {
    StubProvider provider; // deliberately empty: pid -1 resolves to no proto, as in the shipped maps

    auto original = Map::createEmptyMapFile();
    const auto untyped = addSeeded(original, ProtoId::EMPTY_PID, 42); // the engine's "no proto" sentinel
    // A wall behind it: if the untyped record consumed the wrong number of bytes, this one is read
    // from the wrong offset and its base fields come back as garbage.
    const auto wall = addSeeded(original, ProtoId(ObjectType::Wall, 100).pid(), 43);

    const auto reloaded = readMapFile(mapBytes(original, provider), provider);

    const auto& got = reloaded->getMapFile().map_objects.at(0);
    REQUIRE(got.size() == 2);
    checkBase(*got[0], *untyped);
    checkBase(*got[1], *wall);
}

TEST_CASE("MAP read -> write reproduces a shipped map byte for byte", "[map][roundtrip]") {
    // sfshutl2.map: real header words, real per-elevation framing, 227 objects (see test_reading_map).
    const StubProvider provider = sfshutl2Provider();
    const auto original = readAllBytes(dataPath("sfshutl2.map"));
    const auto map = readMapFile(original, provider);

    CHECK(mapBytes(map->getMapFile(), provider) == original);
}

TEST_CASE("MAP write -> read -> write keeps every script section byte for byte", "[map][roundtrip][scripts]") {
    StubProvider provider;
    auto original = Map::createEmptyMapFile();
    // More than one 16-slot block in a section exercises the padding and per-block check words.
    auto& spatial = original.map_scripts[static_cast<int>(MapScript::ScriptType::SPATIAL)];
    for (uint32_t i = 0; i < 21; ++i) {
        spatial.push_back(MapScript::makeSpatialScript(i, 100 + i, 1000 + i, i % 3, 2 + i % 4, 0));
    }
    auto& items = original.map_scripts[static_cast<int>(MapScript::ScriptType::ITEM)];
    for (uint32_t i = 0; i < 3; ++i) {
        items.push_back(MapScript::makeObjectScript(MapScript::ScriptType::ITEM, i, 200 + i, 0x1000 + i));
    }
    auto timer = MapScript::makeObjectScript(MapScript::ScriptType::TIMER, 0, 300, 0x2000);
    timer.timer = 4242;
    original.map_scripts[static_cast<int>(MapScript::ScriptType::TIMER)].push_back(timer);
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        original.scripts_in_section[section] = static_cast<int>(original.map_scripts[section].size());
    }

    const auto first = mapBytes(original, provider);
    const auto reloaded = readMapFile(first, provider);
    const auto& result = reloaded->getMapFile();
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        INFO("section " << section);
        REQUIRE(result.map_scripts[section].size() == original.map_scripts[section].size());
        for (size_t i = 0; i < original.map_scripts[section].size(); ++i) {
            const MapScript& want = original.map_scripts[section][i];
            const MapScript& got = result.map_scripts[section][i];
            CHECK(got.pid == want.pid);
            CHECK(got.timer == want.timer);
            CHECK(got.spatial_radius == want.spatial_radius);
            CHECK(got.script_id == want.script_id);
            CHECK(got.script_oid == want.script_oid);
            CHECK(got.local_var_offset == want.local_var_offset);
        }
    }
    CHECK(mapBytes(result, provider) == first);
}

TEST_CASE("MAP writer derives the inventory count from the inventory", "[map][roundtrip]") {
    StubProvider provider;
    const uint32_t containerPid = ProtoId(ObjectType::Item, 200).pid();
    const uint32_t armorPid = ProtoId(ObjectType::Item, 201).pid();
    provider.addItem(containerPid, ItemType::Container);
    provider.addItem(armorPid, ItemType::Armor);

    auto mapFile = Map::createEmptyMapFile();
    auto container = addSeeded(mapFile, containerPid, 1);
    container->objects_in_inventory = 5; // stale: the inventory holds one item
    container->inventory.push_back(std::make_unique<MapObject>(std::move(*seededObject(armorPid, 2))));
    const auto after = addSeeded(mapFile, ProtoId(ObjectType::Wall, 100).pid(), 3);

    // Writing the stale 5 would make the reader consume four records that aren't there.
    const auto reloaded = readMapFile(mapBytes(mapFile, provider), provider);
    const auto& objects = reloaded->getMapFile().map_objects.at(0);
    REQUIRE(objects.size() == 2);
    CHECK(objects[0]->objects_in_inventory == 1u);
    CHECK(objects[0]->inventory.size() == 1);
    checkBase(*objects[1], *after);
}
