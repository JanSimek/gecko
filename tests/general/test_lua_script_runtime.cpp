#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "reader/map/MapReader.h"
#include "scripting/LuaScriptRuntime.h"
#include "scripting/MapScriptApi.h"
#include "writer/map/MapWriter.h"

#include "support/ControllerFixture.h"
#include "support/ProStubProvider.h"
#include "support/TempFile.h"

using namespace geck;
using geck::test::ControllerFixture;

namespace {
constexpr int ELEV = 0;
constexpr uint16_t EMPTY = static_cast<uint16_t>(Map::EMPTY_TILE);
} // namespace

TEST_CASE("Luau script paints tiles through the host API, as one undo entry", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    const auto r = rt.run(R"(
        for i = 0, 9 do
            api:paintFloor(i, 271)
        end
    )",
        api, fx.controller, "fill");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);

    // Verify the map state the script produced.
    for (int i = 0; i < 10; ++i) {
        CHECK(fx.mapFile().tiles.at(ELEV)[i].getFloor() == 271);
    }
    CHECK(api.paintedTiles() == 10);

    // The whole 10-paint run is ONE undo entry.
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    for (int i = 0; i < 10; ++i) {
        CHECK(fx.mapFile().tiles.at(ELEV)[i].getFloor() == EMPTY);
    }
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("Luau reads a C++ container (hexNeighbors) back as a table", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // hexNeighbors returns std::vector<int>; LuaBridge converts it to a Lua array. The
    // script proves the conversion by counting it and feeding the count back in.
    const auto r = rt.run(R"(
        local n = api:hexNeighbors(100 * 200 + 100)
        assert(#n == 6, "expected 6 neighbours, got " .. #n)
        api:paintFloor(0, 270 + #n)
    )",
        api, fx.controller, "neighbours");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);
    CHECK(fx.mapFile().tiles.at(ELEV)[0].getFloor() == 276);
}

TEST_CASE("Luau scripts are sandboxed", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // Safe-by-default: io is gone, os is trimmed, no source/bytecode loaders.
    const auto ok = rt.run(
        "assert(io == nil, 'io leaked') "
        "assert(os.execute == nil, 'os.execute leaked') "
        "assert(loadstring == nil, 'loadstring leaked')",
        api, fx.controller);
    INFO("script error: " << ok.error);
    CHECK(ok.ok);

    // Actually trying to reach the filesystem errors (the symbol is nil).
    const auto blocked = rt.run("os.execute('echo hi')", api, fx.controller);
    CHECK_FALSE(blocked.ok);
}

TEST_CASE("Luau compile and runtime errors surface as a ScriptResult", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    const auto runtimeErr = rt.run("error('boom')", api, fx.controller);
    CHECK_FALSE(runtimeErr.ok);
    CHECK_FALSE(runtimeErr.error.empty());

    const auto compileErr = rt.run("this is not valid lua", api, fx.controller);
    CHECK_FALSE(compileErr.ok);
    CHECK_FALSE(compileErr.error.empty());

    // A failed run leaves the map unchanged at the assertion point.
    CHECK(fx.mapFile().tiles.at(ELEV)[0].getFloor() == EMPTY);
}

TEST_CASE("Luau-painted tiles survive a map save/reload round-trip", "[scripting][lua][roundtrip]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    const auto r = rt.run(R"(
        api:paintFloor(5, 271)
        api:paintFloor(6, 272)
        api:paintRoof(5, 480)
    )",
        api, fx.controller, "gen");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);

    // Write the script-edited map out and parse it back — the edits must persist through
    // serialization (no objects, so the PRO provider is never consulted).
    geck::test::StubProvider provider;
    geck::test::TempFile tmp{ "geck_lua_roundtrip", ".map" };
    {
        MapWriter writer{ [&](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(tmp.path());
        REQUIRE(writer.write(fx.mapFile()));
    }

    MapReader reader{ [&](uint32_t pid) { return provider.load(pid); } };
    auto reloaded = reader.openFile(tmp.path());
    REQUIRE(reloaded != nullptr);

    const auto& tiles = reloaded->getMapFile().tiles.at(ELEV);
    CHECK(tiles[5].getFloor() == 271);
    CHECK(tiles[6].getFloor() == 272);
    CHECK(tiles[5].getRoof() == 480);
}
