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

    void add(uint32_t pid, Pro::ITEM_TYPE itemType) {
        auto pro = std::make_unique<Pro>(std::filesystem::path("stub"));
        pro->setObjectSubtypeId(static_cast<unsigned int>(itemType));
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

// Asserts every serialized base field survived the round-trip.
void checkBaseFields(const MapObject& got, const MapObject& want) {
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

// MapWriter/MapReader symmetry guard: build a map with a wall, a critter and a
// container holding an inventory item, write it, read it back, and assert the
// objects (incl. the nested inventory item and its amount) survive intact.
TEST_CASE("MAP round-trip preserves objects and inventory", "[map][roundtrip]") {
    StubProvider provider;
    const uint32_t containerPid = pidOf(Pro::OBJECT_TYPE::ITEM, 200);
    const uint32_t armorPid = pidOf(Pro::OBJECT_TYPE::ITEM, 201);
    provider.add(containerPid, Pro::ITEM_TYPE::CONTAINER);
    provider.add(armorPid, Pro::ITEM_TYPE::ARMOR);

    auto original = Map::createEmptyMapFile();

    // A wall, with every base field set to a distinctive value so a field-order
    // or width bug anywhere in the 22-field block is caught.
    auto wall = std::make_shared<MapObject>();
    wall->unknown0 = 0x11111111;
    wall->position = 12345;
    wall->x = 50;
    wall->y = 60;
    wall->sx = -7;
    wall->sy = -9;
    wall->frame_number = 4;
    wall->direction = 3;
    wall->frm_pid = pidOf(Pro::OBJECT_TYPE::WALL, 7);
    wall->flags = 0x04000000;
    wall->elevation = 0;
    wall->pro_pid = pidOf(Pro::OBJECT_TYPE::WALL, 100);
    wall->critter_index = 1234;
    wall->light_radius = 8;
    wall->light_intensity = 65536;
    wall->outline_color = 0xAB;
    wall->map_scripts_pid = 4242;
    wall->script_id = 99;
    wall->max_inventory_size = 0;
    wall->unknown10 = 0x22222222;
    wall->unknown11 = 0x33333333;
    original.map_objects[0].push_back(wall);

    // A critter, exercising the 10-field critter-only block.
    auto critter = std::make_shared<MapObject>();
    critter->pro_pid = pidOf(Pro::OBJECT_TYPE::CRITTER, 50);
    critter->position = 6789;
    critter->elevation = 0;
    critter->player_reaction = 3;
    critter->current_mp = 11;
    critter->combat_results = 22;
    critter->dmg_last_turn = 33;
    critter->ai_packet = 7;
    critter->group_id = 1;
    critter->who_hit_me = 44;
    critter->current_hp = 42;
    critter->current_rad = 0;
    critter->current_poison = 0;
    original.map_objects[0].push_back(critter);

    // A container holding one armor item, exercising the inventory path.
    auto container = std::make_shared<MapObject>();
    container->pro_pid = containerPid;
    container->position = 100;
    container->elevation = 0;
    container->objects_in_inventory = 1;
    container->max_inventory_size = 10;
    auto armor = std::make_unique<MapObject>();
    armor->pro_pid = armorPid;
    armor->position = 200;
    armor->elevation = 0;
    armor->amount = 5;
    container->inventory.push_back(std::move(armor));
    original.map_objects[0].push_back(container);

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

    // Three objects on elevation 0, none on the other elevations.
    REQUIRE(result.map_objects.at(0).size() == 3);
    CHECK(result.map_objects.at(1).empty());
    CHECK(result.map_objects.at(2).empty());

    checkBaseFields(*result.map_objects.at(0)[0], *wall);

    const auto& gotCritter = *result.map_objects.at(0)[1];
    CHECK(gotCritter.pro_pid == critter->pro_pid);
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

    const auto& gotContainer = *result.map_objects.at(0)[2];
    CHECK(gotContainer.pro_pid == containerPid);
    CHECK(gotContainer.objects_in_inventory == 1);
    REQUIRE(gotContainer.inventory.size() == 1);
    CHECK(gotContainer.inventory[0]->pro_pid == armorPid);
    CHECK(gotContainer.inventory[0]->amount == 5);
    CHECK(gotContainer.inventory[0]->position == 200);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
