#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>

#include "support/MapFileIo.h"
#include "support/ScriptApiFixture.h"

namespace geck::test {

/// Author a small blended reference map — a 12x12 checkerboard of ids 271/272 at the top-left of
/// elevation 0 — at <root>/maps/ref.map, so quilt tests have authored tile blending to learn from.
/// The checkerboard's alternation is its whole adjacency model: a correct quilt has no choice but
/// to reproduce it. `root` is a fresh scratch directory (a geck::test::TempDir). Shared by the
/// MapScriptApi and Lua-runtime quilt tests.
inline void writeCheckerboardReference(const std::filesystem::path& root) {
    ScriptApiFixture author{ false };
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 12; ++col) {
            REQUIRE(author.api.paintFloorXY(col, row, static_cast<uint16_t>(271 + (col + row) % 2)));
        }
    }
    writeMapFile(author.mapFile(), root / "maps/ref.map");
}

} // namespace geck::test
