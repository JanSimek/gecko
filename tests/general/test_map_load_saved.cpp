#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "cli/MapAnalyzer.h"
#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "resource/MapFileIO.h"

#include "support/AnalyzeWrittenMap.h"
#include "support/GzipBytes.h"
#include "support/MapFileIo.h"
#include "support/MapObjectBuilder.h"

using nlohmann::json;
using namespace geck;
using namespace geck::test;

namespace {

constexpr auto MAP_HEADER_SAVED = static_cast<uint32_t>(MapHeaderFlags::Saved);
constexpr auto OBJECT_HIDDEN = static_cast<uint32_t>(Pro::ObjectFlags::OBJECT_HIDDEN);

std::shared_ptr<MapObject> addCritter(Map::MapFile& mapFile, int32_t seed, int32_t cid, int32_t hex, int elevation = 0) {
    auto critter = addSeeded(mapFile, ProtoId(ObjectType::Critter, static_cast<uint32_t>(10 + seed)).pid(), seed, elevation);
    critter->critter_index = cid;
    critter->position = hex;
    critter->who_hit_me = static_cast<uint32_t>(-1);
    return critter;
}

} // namespace

// A save slot stores each map gzip-compressed (fallout2-ce loadsave.cc _GameMap2Slot), and a saved map's
// critters carry live combat state in slots that shipped maps leave zero.
TEST_CASE("a save slot's gzip map loads and reports live critter combat state", "[cli][map][saved]") {
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

    TempFile gz{ "geck_saved_map_gzip", ".SAV" };
    writeBytes(gz.path(), gzipBytes(mapBytes(mapFile)));

    resource::GameResources resources;
    std::string error;
    auto loaded = cli::loadMap(resources, gz.path().string(), &error);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->getMapFile().map_objects.at(0).size() == 2);
    // The editor opens files from disk through the same reader, so it opens a save slot's map too.
    const auto opened = resource::readMap(resources, gz.path(), resource::MapSource::Disk);
    CHECK(opened->getMapFile().map_objects.at(0).size() == 2);

    const json root = analyzeMapPath(gz.path());
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

// combatLoad relinks whoHitMe only among the critters it lists again, so a hidden critter neither resolves its own
// id nor shadows a listed critter holding the same stale one, and ids do not cross elevations.
TEST_CASE("a saved map resolves whoHitMe among the non-hidden critters of one elevation", "[cli][map][saved]") {
    auto mapFile = Map::createEmptyMapFile();
    mapFile.header.flags |= MAP_HEADER_SAVED;

    auto hidden = addCritter(mapFile, 1, 4, 18080);
    hidden->flags = OBJECT_HIDDEN;
    hidden->who_hit_me = 5;
    auto attacker = addCritter(mapFile, 2, 5, 18082);
    attacker->who_hit_me = 4;
    addCritter(mapFile, 3, 4, 18084);
    auto upstairs = addCritter(mapFile, 4, 6, 18086, 1);
    upstairs->who_hit_me = 5;

    const json root = analyzeWrittenMap(mapFile, "geck_saved_map_hidden");
    const auto& critters = root.at("maps").at(0).at("critters");
    REQUIRE(critters.size() == 4);
    std::map<int, json> byHex;
    for (const auto& critter : critters) {
        byHex[critter.at("hex").get<int>()] = critter;
    }
    CHECK(byHex.at(18080).at("hidden") == true);
    CHECK(byHex.at(18080).at("combat").at("whoHitMe").is_null());
    CHECK(byHex.at(18082).at("hidden") == false);
    CHECK(byHex.at(18082).at("combat").at("whoHitMe").at("hex") == 18084);
    CHECK(byHex.at(18086).at("combat").at("whoHitMe").is_null());
}

TEST_CASE("an unsaved map reports no combat state", "[cli][map][saved]") {
    auto mapFile = Map::createEmptyMapFile();
    mapFile.header.flags &= ~MAP_HEADER_SAVED;
    addCritter(mapFile, 1, 0, 18082);

    const json root = analyzeWrittenMap(mapFile, "geck_unsaved_map");
    CHECK_FALSE(root.at("maps").at(0).at("critters").at(0).contains("combat"));
}

TEST_CASE("a truncated gzip map fails with the reason", "[cli][map][saved]") {
    auto compressed = gzipBytes(mapBytes(Map::createEmptyMapFile()));
    compressed.resize(compressed.size() / 2);
    TempFile gz{ "geck_truncated_gzip", ".SAV" };
    writeBytes(gz.path(), compressed);

    resource::GameResources resources;
    std::string error;
    CHECK(cli::loadMap(resources, gz.path().string(), &error) == nullptr);
    CHECK(error.find("gzip") != std::string::npos);
}
