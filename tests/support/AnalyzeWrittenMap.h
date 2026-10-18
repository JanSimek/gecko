#pragma once

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <ostream>

#include "cli/MapAnalyzer.h"
#include "format/map/Map.h"
#include "resource/GameResources.h"

#include "support/CliJson.h"
#include "support/MapFileIo.h"
#include "support/ProStubProvider.h"
#include "support/TempFile.h"

namespace geck::test {

/// analyze's parsed JSON for the .map at `path`, with no game data mounted: cli::loadMap reads the
/// file through its disk fallback (raw tile ids survive without tiles.lst).
inline nlohmann::json analyzeMapPath(const std::filesystem::path& path) {
    resource::GameResources resources;
    cli::AnalyzeOptions options;
    options.json = true;
    options.maps = { path.string() };
    return runJson([&](std::ostream& out) { return cli::analyzeMaps(resources, options, out); });
}

/// Writes `mapFile` to a temp .map named after `tempPrefix` and returns analyze's parsed JSON for it.
inline nlohmann::json analyzeWrittenMap(const Map::MapFile& mapFile, const char* tempPrefix,
    const StubProvider& provider = {}) {
    const TempFile out{ tempPrefix, ".map" };
    writeMapFile(mapFile, out.path(), provider);
    return analyzeMapPath(out.path());
}

} // namespace geck::test
