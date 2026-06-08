#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "writer/map/MapWriter.h"

using namespace geck;

namespace {

// Stub PRO provider. Only ITEM/SCENERY objects dereference their Pro during
// (de)serialization (for objectSubtypeId()); walls/critters/misc do not. So a
// minimal in-memory Pro carrying just the subtype is enough, and the whole
// round-trip needs no .map or .pro fixture on disk.
struct StubProvider {
    std::map<uint32_t, std::unique_ptr<Pro>> pros;

    void addItem(uint32_t pid, Pro::ITEM_TYPE t) { set(pid, static_cast<unsigned int>(t)); }
    void addScenery(uint32_t pid, Pro::SCENERY_TYPE t) { set(pid, static_cast<unsigned int>(t)); }

    void set(uint32_t pid, unsigned int subtype) {
        auto pro = std::make_unique<Pro>(std::filesystem::path("stub"));
        pro->setObjectSubtypeId(subtype);
        pros[pid] = std::move(pro);
    }

    Pro* load(uint32_t pid) const {
        auto it = pros.find(pid);
        return it != pros.end() ? it->second.get() : nullptr;
    }
};

uint32_t pidOf(Pro::OBJECT_TYPE type, uint32_t index) {
    return (static_cast<uint32_t>(type) << 24) | index;
}

std::filesystem::path tempMapPath() {
    auto path = std::filesystem::temp_directory_path() / "geck_map_roundtrip.map";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

// Fills every serialized base field with a distinctive seed-derived value, so a
// field-order or width bug anywhere in the 22-field block is caught for each
// object type. pro_pid / elevation / inventory are set by the caller.
void fillBase(MapObject& o, int32_t seed) {
    o.unknown0 = static_cast<uint32_t>(seed * 100 + 1);
    o.position = seed * 100 + 2;
    o.x = static_cast<uint32_t>(seed * 100 + 3);
    o.y = static_cast<uint32_t>(seed * 100 + 4);
    o.sx = -(seed * 100 + 5);
    o.sy = -(seed * 100 + 6);
    o.frame_number = static_cast<uint32_t>(seed * 100 + 7);
    o.direction = static_cast<uint32_t>(seed % 6);
    o.frm_pid = static_cast<uint32_t>(seed * 100 + 9);
    o.flags = static_cast<uint32_t>(seed * 100 + 10);
    o.critter_index = seed * 100 + 11;
    o.light_radius = static_cast<uint32_t>(seed % 9);
    o.light_intensity = static_cast<uint32_t>(seed * 100 + 13);
    o.outline_color = static_cast<uint32_t>(seed % 256);
    o.map_scripts_pid = seed * 100 + 15;
    o.script_id = seed * 100 + 16;
    o.max_inventory_size = static_cast<uint32_t>(seed * 100 + 17);
    o.unknown10 = static_cast<uint32_t>(seed * 100 + 18);
    o.unknown11 = static_cast<uint32_t>(seed * 100 + 19);
}

void checkBase(const MapObject& got, const MapObject& want) {
    CHECK(got.unknown0 == want.unknown0);
    CHECK(got.position == want.position);
    CHECK(got.x == want.x);
    CHECK(got.y == want.y);
    CHECK(got.sx == want.sx);
    CHECK(got.sy == want.sy);
    CHECK(got.frame_number == want.frame_number);
    CHECK(got.direction == want.direction);
    CHECK(got.frm_pid == want.frm_pid);
    CHECK(got.flags == want.flags);
    CHECK(got.elevation == want.elevation);
    CHECK(got.pro_pid == want.pro_pid);
    CHECK(got.critter_index == want.critter_index);
    CHECK(got.light_radius == want.light_radius);
    CHECK(got.light_intensity == want.light_intensity);
    CHECK(got.outline_color == want.outline_color);
    CHECK(got.map_scripts_pid == want.map_scripts_pid);
    CHECK(got.script_id == want.script_id);
    CHECK(got.max_inventory_size == want.max_inventory_size);
    CHECK(got.unknown10 == want.unknown10);
    CHECK(got.unknown11 == want.unknown11);
}

} // namespace

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

    const auto path = tempMapPath();
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

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
