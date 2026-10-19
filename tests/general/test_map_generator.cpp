#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <string>

#include "cli/MapGenerator.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "resource/GameResources.h"
#include "resource/MapFileIO.h"

#include "support/Fixtures.h"
#include "support/MapFileIo.h"
#include "support/TempFile.h"

using namespace geck;

TEST_CASE("gecko-cli generate runs a tile script and writes a reloadable map", "[map_generator][cli][scripting]") {
    geck::test::TempFile scriptFile{ "geck_gen_script", ".luau" };
    geck::test::TempFile mapFile{ "geck_gen_out", ".map" };

    // Tile-only generation needs no game data (paintFloor takes a literal id) and no proto
    // provider on write (no objects), so the whole pipeline is exercised headlessly in CI.
    geck::test::writeFile(scriptFile.path(), "for i = 0, 99 do api:paintFloor(i, 271) end");

    geck::resource::GameResources resources; // nothing mounted
    geck::cli::GenerateOptions options;
    options.scriptPath = scriptFile.path().string();
    options.outPath = mapFile.path().string();
    options.elevation = 0;

    std::ostringstream log;
    REQUIRE(geck::cli::generateMap(resources, options, log) == 0);
    INFO("generate log: " << log.str());

    // Reload and confirm the painted tiles persisted.
    const auto reloaded = geck::test::readMapFile(mapFile.path());
    const auto& tiles = reloaded->getMapFile().tiles.at(0);
    for (int i = 0; i < 100; ++i) {
        CHECK(tiles[i].getFloor() == 271);
    }
}

TEST_CASE("gecko-cli generate --in decorates an existing map", "[map_generator][cli][scripting]") {
    geck::test::TempFile baseScript{ "geck_gen_in_base", ".luau" };
    geck::test::TempFile decorScript{ "geck_gen_in_decor", ".luau" };
    geck::test::TempFile baseMap{ "geck_gen_in_basemap", ".map" };
    geck::test::TempFile outMap{ "geck_gen_in_out", ".map" };

    geck::resource::GameResources resources; // nothing mounted: --in reads straight off disk

    // First run: a base map with tiles 0..49 painted.
    geck::test::writeFile(baseScript.path(), "for i = 0, 49 do api:paintFloor(i, 271) end");
    geck::cli::GenerateOptions base;
    base.scriptPath = baseScript.path().string();
    base.outPath = baseMap.path().string();
    std::ostringstream log;
    REQUIRE(geck::cli::generateMap(resources, base, log) == 0);

    // Second run: decorate the base map with tiles 50..99; the original tiles must survive.
    geck::test::writeFile(decorScript.path(), "for i = 50, 99 do api:paintFloor(i, 300) end");
    geck::cli::GenerateOptions decorate;
    decorate.scriptPath = decorScript.path().string();
    decorate.inPath = baseMap.path().string();
    decorate.outPath = outMap.path().string();
    REQUIRE(geck::cli::generateMap(resources, decorate, log) == 0);
    INFO("generate log: " << log.str());

    const auto reloaded = geck::test::readMapFile(outMap.path());
    const auto& tiles = reloaded->getMapFile().tiles.at(0);
    for (int i = 0; i < 50; ++i) {
        CHECK(tiles[i].getFloor() == 271); // preserved from the input map
    }
    for (int i = 50; i < 100; ++i) {
        CHECK(tiles[i].getFloor() == 300); // added by the decorating script
    }
}

TEST_CASE("gecko-cli generate --count writes N maps with consecutive seeds", "[map_generator][cli][scripting]") {
    geck::test::TempFile scriptFile{ "geck_gen_batch_script", ".luau" };
    // count=3 derives <out>_1.map .. <out>_3.map from the base out path, all in a scratch directory
    // that removes them afterwards.
    const geck::test::TempDir outDir{ "geck_gen_batch" };

    // Encode the run's seed in a tile so each output proves which seed generated it.
    geck::test::writeFile(scriptFile.path(), "api:paintFloor(0, tonumber(args.seed))");

    geck::resource::GameResources resources;
    geck::cli::GenerateOptions options;
    options.scriptPath = scriptFile.path().string();
    options.outPath = (outDir.path() / "batch.map").string();
    options.count = 3;
    options.args["seed"] = "500";

    std::ostringstream log;
    REQUIRE(geck::cli::generateMap(resources, options, log) == 0);
    INFO("generate log: " << log.str());

    for (int i = 0; i < 3; ++i) {
        const auto reloaded = geck::test::readMapFile(outDir.path() / ("batch_" + std::to_string(i + 1) + ".map"));
        CHECK(reloaded->getMapFile().tiles.at(0)[0].getFloor() == 500 + i); // seed = base + i
    }
}

TEST_CASE("gecko-cli generate validates its inputs", "[map_generator][cli][scripting]") {
    geck::resource::GameResources resources;
    std::ostringstream log;
    // Both cases fail before writing, so the file is never created — but route the out path
    // through the build-tree temp dir anyway rather than a world-writable location.
    geck::test::TempFile outFile{ "geck_gen_unused", ".map" };
    const std::string outPath = outFile.path().string();

    SECTION("missing script file is reported") {
        geck::cli::GenerateOptions options;
        options.scriptPath = "/no/such/script.luau";
        options.outPath = outPath;
        CHECK(geck::cli::generateMap(resources, options, log) != 0);
    }

    SECTION("out-of-range elevation is rejected") {
        geck::test::TempFile scriptFile{ "geck_gen_elev", ".luau" };
        geck::test::writeFile(scriptFile.path(), "api:paintFloor(0, 271)");
        geck::cli::GenerateOptions options;
        options.scriptPath = scriptFile.path().string();
        options.outPath = outPath;
        options.elevation = 3;
        CHECK(geck::cli::generateMap(resources, options, log) == 2);
    }

    SECTION("non-positive count is rejected") {
        geck::test::TempFile scriptFile{ "geck_gen_count", ".luau" };
        geck::test::writeFile(scriptFile.path(), "api:paintFloor(0, 271)");
        geck::cli::GenerateOptions options;
        options.scriptPath = scriptFile.path().string();
        options.outPath = outPath;
        options.count = 0;
        CHECK(geck::cli::generateMap(resources, options, log) == 2);
    }

    SECTION("an unreadable --in map is reported") {
        geck::test::TempFile scriptFile{ "geck_gen_bad_in", ".luau" };
        geck::test::writeFile(scriptFile.path(), "api:paintFloor(0, 271)");
        geck::cli::GenerateOptions options;
        options.scriptPath = scriptFile.path().string();
        options.outPath = outPath;
        options.inPath = "/no/such/input.map";
        CHECK(geck::cli::generateMap(resources, options, log) == 2);
    }
}

TEST_CASE("gecko-cli generate stops a script that outruns its time budget", "[map_generator][cli][scripting]") {
    geck::test::TempFile scriptFile{ "geck_gen_loop", ".luau" };
    geck::test::TempFile mapFile{ "geck_gen_loop_out", ".map" };
    geck::test::writeFile(scriptFile.path(), "while true do end");

    geck::resource::GameResources resources;
    geck::cli::GenerateOptions options;
    options.scriptPath = scriptFile.path().string();
    options.outPath = mapFile.path().string();
    options.timeBudgetMs = 200;

    std::ostringstream log;
    CHECK(geck::cli::generateMap(resources, options, log) == 1);
    CHECK(log.str().find("time budget") != std::string::npos);
}

TEST_CASE("gecko-cli generate --in keeps the map edge of the map it decorates", "[map_generator][cli][scripting]") {
    geck::test::TempFile baseScript{ "geck_gen_edge_base", ".luau" };
    geck::test::TempFile baseMap{ "geck_gen_edge_base", ".map" };
    geck::test::TempFile baseEdge{ "geck_gen_edge_base", ".EDG" };
    geck::test::TempFile outMap{ "geck_gen_edge_out", ".map" };
    geck::test::TempFile outEdge{ "geck_gen_edge_out", ".EDG" };
    geck::resource::GameResources resources;

    geck::test::writeFile(baseScript.path(), "api:paintFloor(0, 271)");
    geck::cli::GenerateOptions base;
    base.scriptPath = baseScript.path().string();
    base.outPath = baseMap.path().string();
    std::ostringstream log;
    REQUIRE(geck::cli::generateMap(resources, base, log) == 0);
    std::filesystem::copy_file(geck::test::dataPath("newr2.edg"), baseEdge.path());

    geck::cli::GenerateOptions decorate;
    decorate.scriptPath = baseScript.path().string();
    decorate.inPath = baseMap.path().string();
    decorate.outPath = outMap.path().string();
    REQUIRE(geck::cli::generateMap(resources, decorate, log) == 0);
    INFO("generate log: " << log.str());

    // The output carries the same edge, and reading the output back attaches it.
    REQUIRE(std::filesystem::exists(outEdge.path()));
    const auto reloaded = geck::resource::readMap(resources, outMap.path(), geck::resource::MapSource::Disk);
    REQUIRE(reloaded->edge().has_value());
    CHECK(reloaded->edge()->totalZones() == 4);
}
