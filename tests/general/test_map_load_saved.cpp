#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "cli/MapAnalyzer.h"
#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "writer/map/MapWriter.h"

#include "support/GzipBytes.h"
#include "support/MapObjectBuilder.h"
#include "support/ProStubProvider.h"
#include "support/TempFile.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;

namespace {

constexpr uint32_t MAP_HEADER_SAVED = 0x01;

std::shared_ptr<MapObject> addCritter(Map::MapFile& mapFile, int32_t seed, int32_t cid, int32_t hex) {
    auto critter = std::make_shared<MapObject>();
    fillBase(*critter, seed);
    critter->pro_pid = pidOf(Pro::OBJECT_TYPE::CRITTER, static_cast<uint32_t>(10 + seed));
    critter->elevation = 0;
    critter->critter_index = cid;
    critter->position = hex;
    critter->who_hit_me = static_cast<uint32_t>(-1);
    mapFile.map_objects[0].push_back(critter);
    return critter;
}

void writeMap(const Map::MapFile& mapFile, const std::filesystem::path& path, const StubProvider& provider) {
    MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
    writer.openFile(path);
    REQUIRE(writer.write(mapFile));
}

json analyze(const std::filesystem::path& path) {
    resource::GameResources resources;
    cli::AnalyzeOptions options;
    options.json = true;
    options.maps = { path.string() };
    std::ostringstream out;
    REQUIRE(cli::analyzeMaps(resources, options, out) == 0);
    return json::parse(out.str());
}

} // namespace

// A save slot stores each map gzip-compressed (fallout2-ce loadsave.cc _GameMap2Slot), and a saved map's
// critters carry live combat state in slots that shipped maps leave zero.
TEST_CASE("a save slot's gzip map loads and reports live critter combat state", "[cli][map][saved]") {
    StubProvider provider;
    auto mapFile = Map::createEmptyMapFile();
    mapFile.header.flags |= MAP_HEADER_SAVED;

    auto attacker = addCritter(mapFile, 1, 3, 18082);
    attacker->current_hp = static_cast<uint32_t>(12);
    attacker->current_ap = 7;
    attacker->damage_last_turn = 14;
    attacker->maneuver = 0x01;       // CRITTER_MANEUVER_ENGAGING
    attacker->combat_results = 0x02; // DAM_KNOCKED_DOWN
    attacker->who_hit_me = 4;
    auto victim = addCritter(mapFile, 2, 4, 18084);
    victim->current_hp = static_cast<uint32_t>(-6);
    victim->combat_results = 0x80; // DAM_DEAD

    TempFile plain{ "geck_saved_map_plain", ".map" };
    writeMap(mapFile, plain.path(), provider);
    TempFile gz{ "geck_saved_map_gzip", ".SAV" };
    writeAllBytes(gz.path(), gzipBytes(readAllBytes(plain.path())));

    resource::GameResources resources;
    std::string error;
    auto loaded = cli::loadMap(resources, gz.path().string(), &error);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->getMapFile().map_objects.at(0).size() == 2);

    const json root = analyze(gz.path());
    const auto& critters = root.at("maps").at(0).at("critters");
    REQUIRE(critters.size() == 2);
    const auto& combat = critters.at(0).at("combat");
    CHECK(combat.at("cid") == 3);
    CHECK(combat.at("hp") == 12);
    CHECK(combat.at("ap") == 7);
    CHECK(combat.at("damageLastTurn") == 14);
    CHECK(combat.at("maneuverFlags") == json::array({ "CRITTER_MANEUVER_ENGAGING" }));
    CHECK(combat.at("resultFlags") == json::array({ "DAM_KNOCKED_DOWN" }));
    CHECK(combat.at("whoHitMeCid") == 4);
    CHECK(combat.at("whoHitMe").at("hex") == 18084);
    CHECK(critters.at(1).at("combat").at("hp") == -6);
    CHECK(critters.at(1).at("combat").at("resultFlags") == json::array({ "DAM_DEAD" }));
}

TEST_CASE("an unsaved map reports no combat state", "[cli][map][saved]") {
    StubProvider provider;
    auto mapFile = Map::createEmptyMapFile();
    mapFile.header.flags &= ~MAP_HEADER_SAVED;
    addCritter(mapFile, 1, 0, 18082);

    TempFile plain{ "geck_unsaved_map", ".map" };
    writeMap(mapFile, plain.path(), provider);
    const json root = analyze(plain.path());
    CHECK_FALSE(root.at("maps").at(0).at("critters").at(0).contains("combat"));
}

TEST_CASE("a truncated gzip map fails with the reason", "[cli][map][saved]") {
    StubProvider provider;
    auto mapFile = Map::createEmptyMapFile();
    TempFile plain{ "geck_truncated_plain", ".map" };
    writeMap(mapFile, plain.path(), provider);
    auto compressed = gzipBytes(readAllBytes(plain.path()));
    compressed.resize(compressed.size() / 2);
    TempFile gz{ "geck_truncated_gzip", ".SAV" };
    writeAllBytes(gz.path(), compressed);

    resource::GameResources resources;
    std::string error;
    CHECK(cli::loadMap(resources, gz.path().string(), &error) == nullptr);
    CHECK(error.find("gzip") != std::string::npos);
}

// The engine's _obj_load_obj reads an inventory entry as a full object that may hold its own
// inventory. A container in a critter's pack must not desynchronise the objects after it.
TEST_CASE("MAP reader follows nested inventories", "[map][roundtrip]") {
    StubProvider provider;
    const uint32_t containerPid = pidOf(Pro::OBJECT_TYPE::ITEM, 200);
    const uint32_t armorPid = pidOf(Pro::OBJECT_TYPE::ITEM, 201);
    provider.addItem(containerPid, Pro::ITEM_TYPE::CONTAINER);
    provider.addItem(armorPid, Pro::ITEM_TYPE::ARMOR);

    auto mapFile = Map::createEmptyMapFile();
    auto critter = addCritter(mapFile, 1, 0, 18082);
    auto container = std::make_unique<MapObject>();
    fillBase(*container, 2);
    container->pro_pid = containerPid;
    container->amount = 1;
    auto armor = std::make_unique<MapObject>();
    fillBase(*armor, 3);
    armor->pro_pid = armorPid;
    armor->amount = 4;
    container->objects_in_inventory = 1;
    container->inventory.push_back(std::move(armor));
    critter->objects_in_inventory = 1;
    critter->inventory.push_back(std::move(container));
    auto after = addCritter(mapFile, 4, 1, 18084);

    TempFile out{ "geck_nested_inventory", ".map" };
    writeMap(mapFile, out.path(), provider);

    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    const auto reloaded = reader.openFile(out.path());
    REQUIRE(reloaded != nullptr);
    const auto& objects = reloaded->getMapFile().map_objects.at(0);
    REQUIRE(objects.size() == 2);
    REQUIRE(objects[0]->inventory.size() == 1);
    REQUIRE(objects[0]->inventory[0]->inventory.size() == 1);
    CHECK(objects[0]->inventory[0]->inventory[0]->pro_pid == armorPid);
    CHECK(objects[0]->inventory[0]->inventory[0]->amount == 4);
    CHECK(objects[1]->position == after->position);
}
