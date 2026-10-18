#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "resource/GameResources.h"
#include "resource/MapCompleteness.h"
#include "support/Fixtures.h"
#include "support/MapObjectBuilder.h"

using namespace geck;

// The scans mount f2_res.dat, the committed DAT fixture. It carries interface art but no tiles.lst,
// scripts.lst, or tile art, so every index-backed check degrades to its "not mounted" reason —
// exactly the state the scan must report rather than throw on.

TEST_CASE("scanMapCompleteness reports an empty map as complete", "[map_completeness]") {
    resource::GameResources resources;
    resources.files().addDataPath(test::f2ResDat());
    auto map = test::mapOf();

    const auto report = resource::scanMapCompleteness(resources, *map);

    CHECK(report.complete());
    CHECK(report.missingCount() == 0);
    CHECK(report.usedTileCount == 0);
    CHECK(report.objectArtCount == 0);
    CHECK(report.scriptCount == 0);
    // The fixture DAT ships neither index file; that is mount context, not a missing reference.
    CHECK_FALSE(report.tilesLstMounted);
    CHECK_FALSE(report.scriptsLstMounted);
    REQUIRE(report.mounts.size() == 1);
    CHECK(report.mounts.front().kind == resource::MountedSourceInfo::Kind::Dat);
}

TEST_CASE("scanMapCompleteness reports used tiles that no index can resolve", "[map_completeness]") {
    resource::GameResources resources;
    resources.files().addDataPath(test::f2ResDat());
    auto map = test::mapOf();
    map->getMapFile().tiles.at(0)[100].setFloor(50);
    map->getMapFile().tiles.at(0)[200].setRoof(60);
    map->getMapFile().tiles.at(1)[300].setFloor(50); // same id on another elevation: still one entry

    const auto report = resource::scanMapCompleteness(resources, *map);

    CHECK(report.usedTileCount == 2); // distinct ids 50 and 60
    REQUIRE(report.missingTiles.size() == 2);
    CHECK(report.missingTiles[0].id == 50);
    CHECK(report.missingTiles[0].art.empty());
    CHECK(report.missingTiles[0].reason == "tiles.lst not mounted");
    CHECK_FALSE(report.complete());
}

TEST_CASE("scanMapCompleteness reports unresolvable object sprites, skipping inventory children", "[map_completeness]") {
    resource::GameResources resources;
    resources.files().addDataPath(test::f2ResDat());
    auto map = test::mapOf();

    auto& mapFile = map->getMapFile();
    test::addObject(mapFile, 0, 100)->frm_pid = 0x0F000001u; // FID type 15 does not exist -> resolve() throws
    test::addObject(mapFile, 0, 200)->frm_pid = 0x0F000001u; // same FID: deduplicated
    test::addObject(mapFile, 0, -1)->frm_pid = 0x0F000002u;  // inside a container, never rendered

    const auto report = resource::scanMapCompleteness(resources, *map);

    CHECK(report.objectArtCount == 1);
    REQUIRE(report.missingObjectArt.size() == 1);
    CHECK(report.missingObjectArt[0].fid == 0x0F000001u);
    CHECK(report.missingObjectArt[0].reason == "FID does not resolve");
}

TEST_CASE("scanMapCompleteness reports script references from the header and the script sections", "[map_completeness]") {
    resource::GameResources resources;
    resources.files().addDataPath(test::f2ResDat());
    auto map = test::mapOf();
    map->getMapFile().header.script_id = 42; // 1-based -> program index 41
    map->getMapFile().map_scripts[static_cast<int>(MapScript::ScriptType::CRITTER)].push_back(
        MapScript::makeObjectScript(MapScript::ScriptType::CRITTER, 0, 7, 1));

    const auto report = resource::scanMapCompleteness(resources, *map);

    CHECK(report.scriptCount == 2);
    REQUIRE(report.unresolvedScripts.size() == 2);
    CHECK(report.unresolvedScripts[0].programIndex == 7);
    CHECK(report.unresolvedScripts[1].programIndex == 41);
    CHECK(report.unresolvedScripts[0].reason == "scripts.lst not mounted");
    CHECK(report.unresolvedScripts[0].name.empty());
}
