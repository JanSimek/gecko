#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

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
        MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(tmp.path());
        REQUIRE(writer.write(fx.mapFile()));
    }

    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    auto reloaded = reader.openFile(tmp.path());
    REQUIRE(reloaded != nullptr);

    const auto& tiles = reloaded->getMapFile().tiles.at(ELEV);
    CHECK(tiles[5].getFloor() == 271);
    CHECK(tiles[6].getFloor() == 272);
    CHECK(tiles[5].getRoof() == 480);
}

TEST_CASE("Luau can reach the name resolvers", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // tileId, mapScenery and placeProto are bound and callable. Headless they resolve to
    // -1/empty/false (no data) — exactly the contract a generator branches on.
    const auto r = rt.run(R"(
        assert(api:tileId("edg5000") == -1, "expected -1 without data")
        assert(#api:mapScenery("maps/desert1.map") == 0, "expected no scenery without data")
        assert(api:placeProto(0x02000066, 20100, 0) == false, "expected false without data")
    )",
        api, fx.controller, "resolvers");
    INFO("script error: " << r.error);
    CHECK(r.ok);
}

TEST_CASE("Luau exposes caller args as the global table", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    const ScriptArgs args{ { "seed", "42" }, { "tile", "edg5000" } };
    const auto r = rt.run(R"(
        assert(args ~= nil, "args table missing")
        assert(tonumber(args.seed) == 42, "args.seed wrong")
        assert(args.tile == "edg5000", "args.tile wrong")
        assert(args.missing == nil, "absent arg should be nil")
    )",
        api, fx.controller, "args", args);
    INFO("script error: " << r.error);
    CHECK(r.ok);
}

TEST_CASE("The shipped desert_terrain.luau compiles and guards on missing data", "[scripting][lua]") {
    std::ifstream file(std::string(GECK_SCRIPTS_DIR) + "/desert_terrain.luau");
    REQUIRE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // Headless (no data): tileId("edg5000") is -1, so the example must abort cleanly with a
    // hint rather than paint a bogus tile or error. This keeps the committed script CI-checked
    // for syntax and its guard path; the live fill/scatter runs in the GUI (needs data + GL).
    const auto r = rt.run(source, api, fx.controller, "desert");
    INFO("script error: " << r.error);
    CHECK(r.ok);
    CHECK(api.paintedTiles() == 0);
    CHECK(api.placedObjects() == 0);
    CHECK(r.output.find("Mount Fallout 2 data") != std::string::npos);
}

TEST_CASE("Luau places objects headlessly (data only) and they survive save/reload", "[scripting][lua][roundtrip]") {
    ControllerFixture fx;
    // Headless data-only mode: no GL, so placeObject records map data without a sprite.
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, /*buildSprites*/ false);
    LuaScriptRuntime rt;

    // Scrub at two hexes. placeProto needs a proto to read its FID (no data here), so the
    // generator-style call uses placeObject with explicit ids, which is all the .map stores.
    const auto r = rt.run(R"(
        assert(api:placeObject(0x02000066, 0x02000000, 20100, 0))
        assert(api:placeObject(0x02000066, 0x02000000, 20102, 1))
    )",
        api, fx.controller, "scatter");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);
    REQUIRE(fx.mapFile().map_objects.at(ELEV).size() == 2);

    geck::test::StubProvider provider;
    // Scenery objects read their subtype from the proto during (de)serialization.
    provider.addScenery(0x02000066u, Pro::SCENERY_TYPE::GENERIC);
    geck::test::TempFile tmp{ "geck_lua_objects", ".map" };
    {
        MapWriter writer{ [&provider](int32_t pid) { return provider.load(static_cast<uint32_t>(pid)); } };
        writer.openFile(tmp.path());
        REQUIRE(writer.write(fx.mapFile()));
    }

    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    auto reloaded = reader.openFile(tmp.path());
    REQUIRE(reloaded != nullptr);

    const auto& objs = reloaded->getMapFile().map_objects.at(ELEV);
    REQUIRE(objs.size() == 2);
    CHECK(objs[0]->pro_pid == 0x02000066u);
    CHECK(objs[0]->position == 20100);
    CHECK(objs[1]->position == 20102);
}

TEST_CASE("Luau print() output is captured for the console", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // Multiple args are tab-separated, each print() ends with a newline (Lua print semantics).
    const auto r = rt.run(R"(
        print("hello", 42)
        print("done")
    )",
        api, fx.controller, "print");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);
    CHECK(r.output == "hello\t42\ndone\n");
}
