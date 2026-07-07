#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>

#include "cli/MapGenerator.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"

#include "support/ProStubProvider.h"
#include "support/TempFile.h"

using namespace geck;

namespace {

// Writes `source` to a scratch .luau file and returns its path.
std::filesystem::path writeScript(geck::test::TempFile& file, const std::string& source) {
    std::ofstream out(file.path(), std::ios::binary);
    out << source;
    out.close();
    return file.path();
}

} // namespace

TEST_CASE("gecko-cli generate runs a tile script and writes a reloadable map", "[cli][generate]") {
    geck::test::TempFile scriptFile{ "geck_gen_script", ".luau" };
    geck::test::TempFile mapFile{ "geck_gen_out", ".map" };

    // Tile-only generation needs no game data (paintFloor takes a literal id) and no proto
    // provider on write (no objects), so the whole pipeline is exercised headlessly in CI.
    writeScript(scriptFile, "for i = 0, 99 do api:paintFloor(i, 271) end");

    geck::resource::GameResources resources; // nothing mounted
    geck::cli::GenerateOptions options;
    options.scriptPath = scriptFile.path().string();
    options.outPath = mapFile.path().string();
    options.elevation = 0;

    std::ostringstream log;
    REQUIRE(geck::cli::generateMap(resources, options, log) == 0);
    INFO("generate log: " << log.str());

    // Reload and confirm the painted tiles persisted.
    geck::test::StubProvider provider;
    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    auto reloaded = reader.openFile(mapFile.path());
    REQUIRE(reloaded != nullptr);
    const auto& tiles = reloaded->getMapFile().tiles.at(0);
    for (int i = 0; i < 100; ++i) {
        CHECK(tiles[i].getFloor() == 271);
    }
}

TEST_CASE("gecko-cli generate --in decorates an existing map", "[cli][generate]") {
    geck::test::TempFile baseScript{ "geck_gen_in_base", ".luau" };
    geck::test::TempFile decorScript{ "geck_gen_in_decor", ".luau" };
    geck::test::TempFile baseMap{ "geck_gen_in_basemap", ".map" };
    geck::test::TempFile outMap{ "geck_gen_in_out", ".map" };

    geck::resource::GameResources resources; // nothing mounted: --in reads straight off disk

    // First run: a base map with tiles 0..49 painted.
    writeScript(baseScript, "for i = 0, 49 do api:paintFloor(i, 271) end");
    geck::cli::GenerateOptions base;
    base.scriptPath = baseScript.path().string();
    base.outPath = baseMap.path().string();
    std::ostringstream log;
    REQUIRE(geck::cli::generateMap(resources, base, log) == 0);

    // Second run: decorate the base map with tiles 50..99; the original tiles must survive.
    writeScript(decorScript, "for i = 50, 99 do api:paintFloor(i, 300) end");
    geck::cli::GenerateOptions decorate;
    decorate.scriptPath = decorScript.path().string();
    decorate.inPath = baseMap.path().string();
    decorate.outPath = outMap.path().string();
    REQUIRE(geck::cli::generateMap(resources, decorate, log) == 0);
    INFO("generate log: " << log.str());

    geck::test::StubProvider provider;
    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    auto reloaded = reader.openFile(outMap.path());
    REQUIRE(reloaded != nullptr);
    const auto& tiles = reloaded->getMapFile().tiles.at(0);
    for (int i = 0; i < 50; ++i) {
        CHECK(tiles[i].getFloor() == 271); // preserved from the input map
    }
    for (int i = 50; i < 100; ++i) {
        CHECK(tiles[i].getFloor() == 300); // added by the decorating script
    }
}

TEST_CASE("gecko-cli generate --count writes N maps with consecutive seeds", "[cli][generate]") {
    geck::test::TempFile scriptFile{ "geck_gen_batch_script", ".luau" };
    // count=3 derives <out>_1.map .. <out>_3.map from the base out path; declare each derived
    // name as a TempFile so stale copies are removed up front and the outputs are cleaned up.
    geck::test::TempFile out1{ "geck_gen_batch_1", ".map" };
    geck::test::TempFile out2{ "geck_gen_batch_2", ".map" };
    geck::test::TempFile out3{ "geck_gen_batch_3", ".map" };

    // Encode the run's seed in a tile so each output proves which seed generated it.
    writeScript(scriptFile, "api:paintFloor(0, tonumber(args.seed))");

    geck::resource::GameResources resources;
    geck::cli::GenerateOptions options;
    options.scriptPath = scriptFile.path().string();
    options.outPath = (out1.path().parent_path() / "geck_gen_batch.map").string();
    options.count = 3;
    options.args["seed"] = "500";

    std::ostringstream log;
    REQUIRE(geck::cli::generateMap(resources, options, log) == 0);
    INFO("generate log: " << log.str());

    geck::test::StubProvider provider;
    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    const std::array<const std::filesystem::path*, 3> outs{ &out1.path(), &out2.path(), &out3.path() };
    for (int i = 0; i < 3; ++i) {
        auto reloaded = reader.openFile(*outs[i]);
        REQUIRE(reloaded != nullptr);
        CHECK(reloaded->getMapFile().tiles.at(0)[0].getFloor() == 500 + i); // seed = base + i
    }
}

TEST_CASE("gecko-cli generate validates its inputs", "[cli][generate]") {
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
        writeScript(scriptFile, "api:paintFloor(0, 271)");
        geck::cli::GenerateOptions options;
        options.scriptPath = scriptFile.path().string();
        options.outPath = outPath;
        options.elevation = 3;
        CHECK(geck::cli::generateMap(resources, options, log) == 2);
    }

    SECTION("non-positive count is rejected") {
        geck::test::TempFile scriptFile{ "geck_gen_count", ".luau" };
        writeScript(scriptFile, "api:paintFloor(0, 271)");
        geck::cli::GenerateOptions options;
        options.scriptPath = scriptFile.path().string();
        options.outPath = outPath;
        options.count = 0;
        CHECK(geck::cli::generateMap(resources, options, log) == 2);
    }

    SECTION("an unreadable --in map is reported") {
        geck::test::TempFile scriptFile{ "geck_gen_bad_in", ".luau" };
        writeScript(scriptFile, "api:paintFloor(0, 271)");
        geck::cli::GenerateOptions options;
        options.scriptPath = scriptFile.path().string();
        options.outPath = outPath;
        options.inPath = "/no/such/input.map";
        CHECK(geck::cli::generateMap(resources, options, log) == 2);
    }
}
