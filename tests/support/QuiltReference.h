#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <system_error>

#include "format/pro/Pro.h"
#include "scripting/MapScriptApi.h"
#include "writer/map/MapWriter.h"

#include "support/ControllerFixture.h"

namespace geck::test {

/// Author a small blended reference map — a 12x12 checkerboard of ids 271/272 at the top-left of
/// elevation 0 — at <root>/maps/ref.map, so quilt tests have authored tile blending to learn from.
/// The checkerboard's alternation is its whole adjacency model: a correct quilt has no choice but
/// to reproduce it. Shared by the MapScriptApi and Lua-runtime quilt tests.
inline void writeCheckerboardReference(const std::filesystem::path& root) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "maps");
    ControllerFixture author;
    MapScriptApi authorApi(author.resources, author.hexgrid, author.controller, *author.map,
        /*elevation*/ 0, /*buildSprites*/ false);
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 12; ++col) {
            REQUIRE(authorApi.paintFloorXY(col, row, static_cast<uint16_t>(271 + (col + row) % 2)));
        }
    }
    MapWriter writer{ [](int32_t) -> Pro* { return nullptr; } };
    writer.openFile(root / "maps/ref.map");
    REQUIRE(writer.write(author.map->getMapFile()));
}

} // namespace geck::test
