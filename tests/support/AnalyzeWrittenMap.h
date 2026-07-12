#pragma once

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <sstream>

#include "cli/MapAnalyzer.h"
#include "format/map/Map.h"
#include "resource/GameResources.h"
#include "writer/map/MapWriter.h"

#include "support/ProStubProvider.h"
#include "support/TempFile.h"

namespace geck::test {

/// Write `mapFile` to a temp .map and return analyze's parsed JSON. No game data is mounted, so
/// analyze reads the file back via cli::loadMap's disk fallback (raw tile ids survive without
/// tiles.lst). Shared by the analyzer script/adjacency tests.
inline nlohmann::json analyzeWrittenMap(Map::MapFile mapFile, const char* mapName, const char* tempPrefix,
    const StubProvider& provider) {
    Map map{ mapName };
    map.setMapFile(std::make_unique<Map::MapFile>(std::move(mapFile)));

    TempFile out{ tempPrefix, ".map" };
    {
        MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(out.path());
        REQUIRE(writer.write(map.getMapFile()));
    } // flush + close before analyze reads it back

    resource::GameResources resources;
    cli::AnalyzeOptions options;
    options.json = true;
    options.maps = { out.path().string() };
    std::ostringstream jsonOut;
    REQUIRE(cli::analyzeMaps(resources, options, jsonOut) == 0);
    return nlohmann::json::parse(jsonOut.str());
}

} // namespace geck::test
