#include <catch2/catch_test_macros.hpp>

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
}
