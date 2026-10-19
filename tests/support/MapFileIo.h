#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "format/map/Map.h"
#include "reader/map/MapReader.h"
#include "writer/map/MapWriter.h"

#include "support/ProStubProvider.h"
#include "support/TempFile.h"

namespace geck::test {

/// Writes `mapFile` to `path` with MapWriter, taking PROs from `provider`; the file is closed when
/// this returns. Walls, critters and misc objects need no PRO, so most callers pass none.
inline void writeMapFile(const Map::MapFile& mapFile, const std::filesystem::path& path,
    const StubProvider& provider = {}) {
    MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
    writer.openFile(path);
    REQUIRE(writer.write(mapFile));
}

/// The bytes MapWriter produces for `mapFile`.
inline std::vector<uint8_t> mapBytes(const Map::MapFile& mapFile, const StubProvider& provider = {}) {
    const TempFile file{ "geck_map_bytes", ".map" };
    writeMapFile(mapFile, file.path(), provider);
    return readAllBytes(file.path());
}

/// Reads the .map at `path` with MapReader, taking PROs from `provider`.
inline std::unique_ptr<Map> readMapFile(const std::filesystem::path& path, const StubProvider& provider = {}) {
    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    auto map = reader.openFile(path);
    REQUIRE(map != nullptr);
    return map;
}

/// Reads a .map held in memory.
inline std::unique_ptr<Map> readMapFile(const std::vector<uint8_t>& bytes, const StubProvider& provider = {}) {
    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    auto map = reader.openFile("memory.map", bytes);
    REQUIRE(map != nullptr);
    return map;
}

} // namespace geck::test
