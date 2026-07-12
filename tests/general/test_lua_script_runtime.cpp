#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include "editor/TileChange.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "pattern/FillPlan.h"
#include "pattern/PlacementBatch.h"
#include "reader/map/MapReader.h"
#include "scripting/EditArea.h"
#include "scripting/LuaSandboxHost.h"
#include "scripting/LuaScriptRuntime.h"
#include "scripting/MapScriptApi.h"
#include "scripting/ScriptApiReference.h"
#include "writer/map/MapWriter.h"

#include "support/ControllerFixture.h"
#include "support/ProStubProvider.h"
#include "support/QuiltReference.h"
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

TEST_CASE("A runaway Luau script is aborted by the time budget", "[scripting][lua][watchdog]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // An infinite loop would hang the editor's live preview without a guard. A small wall-clock
    // budget arms the safepoint interrupt, which aborts the run as a runtime error (not a silent
    // stop) once the deadline passes — the GUI fill path relies on exactly this.
    const auto r = rt.run("local n = 0 while true do n = n + 1 end", api, fx.controller, "runaway",
        {}, /*timeBudgetMs*/ 50);
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("time budget") != std::string::npos);

    // A bounded script well inside the same budget still completes normally — the watchdog only
    // fires past the deadline.
    const auto ok = rt.run("local s = 0 for i = 1, 1000 do s = s + i end api:paintFloor(0, 271)",
        api, fx.controller, "bounded", {}, /*timeBudgetMs*/ 2000);
    INFO("script error: " << ok.error);
    CHECK(ok.ok);
    CHECK(fx.mapFile().tiles.at(ELEV)[0].getFloor() == 271);
}

// The watchdog raises a plain Lua error, so a script's own pcall/xpcall sees it like any other. If
// the interrupt disarmed itself when it fired, catching it once would buy the script an unwatched
// run — an editor hang. xpcall is checked too because its handler runs Lua, and so hits safepoints
// itself; Luau runs the handler protected, so the re-raise degrades to "error in error handling"
// rather than recursing. The loop after the catch is small and bounded: a healthy watchdog aborts on
// its first back-edge, so the bound never gates the passing path, while a regression reaches the
// print() and fails this test in milliseconds rather than hanging the suite.
TEST_CASE("A script cannot catch its way past the time budget", "[scripting][lua][watchdog]") {
    for (const std::string& guard : { std::string("pcall(runaway)"),
             std::string("xpcall(runaway, function(e) return e end)") }) {
        INFO("guard: " << guard);
        ControllerFixture fx;
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
        LuaScriptRuntime rt;

        const std::string source = "local function runaway() while true do end end\n" + guard
            + "\nlocal n = 0\nfor i = 1, 100000 do n = n + 1 end\nprint('escaped')\n";
        const auto r = rt.run(source, api, fx.controller, "escape", {}, /*timeBudgetMs*/ 50);

        CHECK_FALSE(r.ok);
        CHECK(r.error.find("time budget") != std::string::npos);
        CHECK(r.output.find("escaped") == std::string::npos);
    }
}

TEST_CASE("A non-string error object is reported, not dereferenced", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // lua_tostring returns nullptr for a table, so building the message from it unchecked would be
    // a strlen(nullptr) crash — reachable from one line in the Script Console.
    const auto r = rt.run("error({ code = 1 })", api, fx.controller, "table error");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("non-string error object") != std::string::npos);
}

TEST_CASE("budget 0 means no limit (CLI/batch generation is untimed)", "[scripting][lua][watchdog]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // The default budget (0) installs no interrupt at all, so a long-but-finite trusted run is not
    // capped. Nested loops do millions of back-edges — every one a safepoint that WOULD abort the
    // run if an interrupt were wrongly armed at budget 0; completing proves none is.
    const auto r = rt.run(
        "local s = 0 for i = 1, 3000 do for j = 1, 3000 do s = s + 1 end end print(s)",
        api, fx.controller, "untimed");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);
    CHECK(r.output == "9000000\n");
}

TEST_CASE("A Luau fill paints only the selection, recorded into the plan sink", "[scripting][lua][fill]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // A small floor-tile selection (EditArea requires ascending order): two partial rows of the
    // 100-wide tile grid.
    EditArea area;
    area.floorTiles = { 0, 1, 2, 100, 101, 102 };
    pattern::FillPlan plan;
    api.setArea(&area);
    api.setPlanSink(&plan);

    // A deterministic per-tile pattern over the selection: parity of (col+row) chooses tile A/B.
    // (Inline, not a shipped script — this exercises the sink mechanism with a predictable result.)
    const auto r = rt.run(R"(
        for _, t in ipairs(api:areaFloorTiles()) do
            local parity = (api:tileCol(t) + api:tileRow(t)) % 2
            api:paintFloor(t, (parity == 0) and 271 or 300)
        end
    )",
        api, fx.controller, "checkerboard");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);

    // One recorded tile change per selected tile, and NOTHING committed to the live map (sink mode).
    REQUIRE(plan.tiles.size() == area.floorTiles.size());
    CHECK(api.paintedTiles() == static_cast<int>(area.floorTiles.size()));
    for (const int t : area.floorTiles) {
        CHECK(fx.mapFile().tiles.at(ELEV)[t].getFloor() == EMPTY); // sink => live map untouched
    }

    // Each recorded tile carries the checkerboard value for its own (col,row) parity.
    for (const TileChange& tc : plan.tiles) {
        const int col = tc.tileIndex % 100;
        const int row = tc.tileIndex / 100;
        const uint16_t expected = ((col + row) % 2 == 0) ? 271 : 300;
        CHECK_FALSE(tc.isRoof);
        CHECK(tc.after == expected);
    }

    // The undo stack is untouched: a preview records into the plan but commits no command.
    CHECK_FALSE(fx.undoStack.canUndo());

    // Apply == preview: replaying the SAME plan (what applyFillPreview does) now commits exactly the
    // recorded tiles, as ONE undo entry — so the applied map matches what the preview recorded.
    const auto applied = pattern::PlacementBatch::replay(
        fx.controller, plan, /*buildSprites*/ false, "Fill: checkerboard");
    CHECK(applied.tilesPainted == static_cast<int>(plan.tiles.size()));
    for (const TileChange& tc : plan.tiles) {
        CHECK(fx.mapFile().tiles.at(ELEV)[tc.tileIndex].getFloor() == tc.after);
    }
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    for (const int t : area.floorTiles) {
        CHECK(fx.mapFile().tiles.at(ELEV)[t].getFloor() == EMPTY); // one Ctrl-Z reverts the whole fill
    }
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("The bundled biome fills paint the selection's floor by tile id, headlessly", "[scripting][lua][fill]") {
    // The biome fills paint the floor with numeric tile ids (no data needed) and try to scatter
    // scenery (placeProtoXY -> false without data, never raising). So headless they must run clean and
    // record exactly the selection's floor tiles, with no scenery.
    for (const std::string& script :
        { std::string("desert"), std::string("woods"), std::string("badlands"), std::string("overgrown") }) {
        INFO("script: " << script);
        std::ifstream file(std::string(GECK_SCRIPTS_DIR) + "/fills/" + script + ".luau");
        REQUIRE(file.is_open());
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();

        ControllerFixture fx;
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
        LuaScriptRuntime rt;

        EditArea area;
        area.floorTiles = { 0, 1, 2, 100, 101, 102 }; // floor only -> areaHexes() empty -> no scatter
        pattern::FillPlan plan;
        api.setArea(&area);
        api.setPlanSink(&plan);

        const auto r = rt.run(source, api, fx.controller, "biome-bundled");
        INFO("script error: " << r.error);
        REQUIRE(r.ok);
        CHECK(plan.tiles.size() == area.floorTiles.size());
        CHECK(plan.objects.empty()); // scenery needs data; headless it is skipped, not faked
        // Every painted tile id came from the script's grounded GROUND palette (a real floor id).
        for (const TileChange& tc : plan.tiles) {
            CHECK_FALSE(tc.isRoof);
            CHECK(tc.after != EMPTY);
        }
        // Live map untouched during the (sink) preview.
        for (const int t : area.floorTiles) {
            CHECK(fx.mapFile().tiles.at(ELEV)[t].getFloor() == EMPTY);
        }
    }
}

TEST_CASE("The bundled quilt fills synthesize the selection's floor from a reference", "[scripting][lua][fill]") {
    // Unlike the palette fills, the quilt fills learn from a reference MAP, so mount one (the
    // shared checkerboard) and point args.ref at it. No tiles.lst is mounted: the bld2043
    // exclusion inside the scripts must degrade gracefully (pcall), not abort the fill.
    const auto root = std::filesystem::temp_directory_path() / "geck_lua_quilt_fill"; // NOSONAR: throwaway test dir
    geck::test::writeCheckerboardReference(root);

    for (const std::string& script : { std::string("quilt_desert"), std::string("quilt_mountain") }) {
        INFO("script: " << script);
        std::ifstream file(std::string(GECK_SCRIPTS_DIR) + "/fills/" + script + ".luau");
        REQUIRE(file.is_open());
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();

        ControllerFixture fx;
        fx.resources.files().addDataPath(root);
        MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);

        EditArea area;
        area.floorTiles = { 0, 1, 2, 100, 101, 102 };
        pattern::FillPlan plan;
        api.setArea(&area);
        api.setPlanSink(&plan);

        LuaScriptRuntime rt;
        const auto r = rt.run(source, api, fx.controller, "quilt-fill",
            { { "ref", "maps/ref.map" }, { "groundSrc", "0,0,11,11" }, { "cliff", "off" } });
        INFO("script error: " << r.error);
        REQUIRE(r.ok);
        CHECK(plan.tiles.size() == area.floorTiles.size());
        for (const TileChange& tc : plan.tiles) {
            CHECK_FALSE(tc.isRoof);
            CHECK((tc.after == 271 || tc.after == 272)); // only the reference's vocabulary
        }
        // Live map untouched during the (sink) preview.
        for (const int t : area.floorTiles) {
            CHECK(fx.mapFile().tiles.at(ELEV)[t].getFloor() == EMPTY);
        }
    }

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("A Luau fill scatters objects into the plan sink, replayable as one undo entry", "[scripting][lua][fill]") {
    ControllerFixture fx;
    // Headless/data-only: placeObject records map data with a null visual; replay commits it as data.
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, /*buildSprites*/ false);
    LuaScriptRuntime rt;

    EditArea area;
    area.hexes = { 20100, 20102, 20104 };
    pattern::FillPlan plan;
    api.setArea(&area);
    api.setPlanSink(&plan);

    // Scatter Scrub at each selected hex via placeObject (explicit ids, so no data is needed).
    const auto r = rt.run(R"(
        for _, h in ipairs(api:areaHexes()) do
            api:placeObject(0x02000066, 0x02000000, h, 0)
        end
    )",
        api, fx.controller, "scatter");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);

    // Recorded into the plan, NOT committed to the live map, and no undo entry yet (it's a preview).
    REQUIRE(plan.objects.size() == area.hexes.size());
    CHECK(fx.mapFile().map_objects.at(ELEV).empty());
    CHECK_FALSE(fx.undoStack.canUndo());
    for (const auto& entry : plan.objects) {
        REQUIRE(entry.mapObject);
        CHECK(entry.mapObject->pro_pid == 0x02000066u);
    }

    // Apply == preview: replaying the plan commits exactly those objects, as one undo entry.
    const auto applied = pattern::PlacementBatch::replay(
        fx.controller, plan, /*buildSprites*/ false, "Fill: scatter");
    CHECK(applied.objectsPlaced == static_cast<int>(area.hexes.size()));
    REQUIRE(fx.mapFile().map_objects.at(ELEV).size() == area.hexes.size());
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK(fx.mapFile().map_objects.at(ELEV).empty()); // one Ctrl-Z removes the whole scatter
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

TEST_CASE("Luau passes a Lua table into quiltFloorTiles and quilts the region", "[scripting][lua]") {
    // The inverse marshaling direction of the hexNeighbors case: quiltFloorTiles takes a
    // std::vector<int> PARAMETER, so a Lua array must convert on the way in. The reference is a
    // 12x12 checkerboard authored to disk; the quilted region must reproduce its vocabulary.
    const auto root = std::filesystem::temp_directory_path() / "geck_lua_quilt"; // NOSONAR: throwaway test dir
    geck::test::writeCheckerboardReference(root);

    ControllerFixture fx;
    fx.resources.files().addDataPath(root);
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    const auto r = rt.run(R"(
        local tiles = {}
        for i = 0, 24 do tiles[#tiles + 1] = i end
        local painted = api:quiltFloorTiles("maps/ref.map", 0, tiles)
        assert(painted == 25, "expected 25 painted, got " .. painted)
        assert(api:quiltFloorRect("maps/ref.map", 0, 10, 10, 12, 12) == 9)
    )",
        api, fx.controller, "quilt");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);
    CHECK(api.paintedTiles() == 34);
    for (int i = 0; i < 25; ++i) {
        const uint16_t id = fx.mapFile().tiles.at(ELEV)[i].getFloor();
        CHECK((id == 271 || id == 272));
    }

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
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

TEST_CASE("Luau surfaces genuine failures as errors, not-applicable as values", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // Unified error model: a genuine failure (here, no data mounted) RAISES, so a script can pcall
    // to handle it and the run otherwise reports it — instead of a silently-empty result. Things
    // that are merely "not applicable" stay ordinary return values: placeProto -> false (skip the
    // hex), listMaps -> {} (no maps is a valid answer), noise2d is pure.
    const auto r = rt.run(R"lua(
        assert(not pcall(function() return api:tileId("edg5000") end), "tileId should raise without data")
        assert(not pcall(function() return api:mapScenery("maps/desert1.map") end), "mapScenery should raise")
        assert(not pcall(function() return api:mapSceneryHistogram("maps/desert1.map") end), "histogram should raise")
        assert(not pcall(function() return api:mapFloorTiles("maps/desert1.map") end), "floorTiles should raise")
        assert(not pcall(function() return api:protoName(0x02000066) end), "protoName should raise without data")
        assert(#api:listMaps() == 0, "listMaps is graceful -> empty")
        assert(api:placeProto(0x02000066, 20100, 0) == false, "placeProto returns false, not raise")
        local n = api:noise2d(1.5, 2.5)
        assert(n >= 0 and n <= 1, "noise2d in [0,1]")
        assert(api:proto("scenery", 102) == 0x02000066, "proto builds the right PID")
        assert(not pcall(function() return api:proto("nope", 1) end), "proto raises on unknown type")
        assert(api:hexIndex(5, 3) == 3 * 200 + 5, "hexIndex converts (col,row)")
        assert(api:tileIndex(5, 3) == 3 * 100 + 5, "tileIndex converts (col,row)")
        assert(api:paintFloorXY(5, 3, 271), "paintFloorXY paints a tile")
        assert(api:getFloorXY(5, 3) == 271, "getFloorXY reads it back")
        assert(api:tileCol(api:tileIndex(5, 3)) == 5, "tile (col,row) round-trips")
    )lua",
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

TEST_CASE("Each run is randomly seeded, but an explicit seed reproduces", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // Eight draws make an accidental collision between two independently-seeded runs astronomically
    // unlikely, so this reliably proves the host seeds math.random with fresh entropy each run.
    const std::string draw = "for i = 1, 8 do print(math.random(1, 1000000)) end";

    const auto a = rt.run(draw, api, fx.controller, "rng");
    const auto b = rt.run(draw, api, fx.controller, "rng");
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(a.output != b.output); // default: a different layout every run (the Script Console fix)

    // A script that seeds itself is reproducible (gecko-cli --arg seed=N relies on this).
    const std::string seeded = "math.randomseed(123)\n" + draw;
    const auto c = rt.run(seeded, api, fx.controller, "rng");
    const auto d = rt.run(seeded, api, fx.controller, "rng");
    REQUIRE(c.ok);
    REQUIRE(d.ok);
    CHECK(c.output == d.output);
}

TEST_CASE("The run's seed also drives api:rng", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    const std::string draw = "for i = 1, 8 do print(api:rngInt(1, 1000000)) end";

    // The resolved seed is applied to the api stream exactly as to math.random: an explicit seed
    // reproduces api:rng/rngInt across runs (gecko-cli --count relies on this to vary its batch)...
    const ScriptArgs seeded{ { "seed", "123" } };
    const auto a = rt.run(draw, api, fx.controller, "rng", seeded);
    const auto b = rt.run(draw, api, fx.controller, "rng", seeded);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(a.output == b.output);

    // ...and without one, each run gets a fresh stream instead of mt19937's fixed default.
    const auto c = rt.run(draw, api, fx.controller, "rng");
    const auto d = rt.run(draw, api, fx.controller, "rng");
    REQUIRE(c.ok);
    REQUIRE(d.ok);
    CHECK(c.output != d.output);
    CHECK(a.output != c.output);
}

TEST_CASE("The run's seed is published as args.seed", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    const std::string echo = "print(args.seed)";

    // Without --arg seed the host fills args.seed with a fresh value, so it is present and two runs
    // report different seeds — letting a script print "re-run with --arg seed=N".
    const auto a = rt.run(echo, api, fx.controller, "seed");
    const auto b = rt.run(echo, api, fx.controller, "seed");
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK_FALSE(a.output.empty());
    CHECK(a.output != b.output);

    // An explicit seed is echoed back verbatim, so a layout can be reproduced from what was printed.
    const auto c = rt.run(echo, api, fx.controller, "seed", ScriptArgs{ { "seed", "12345" } });
    REQUIRE(c.ok);
    CHECK(c.output == "12345\n");
}

TEST_CASE("The shipped terrain.luau fails clearly when data is missing", "[scripting][lua]") {
    std::ifstream file(std::string(GECK_SCRIPTS_DIR) + "/editor/terrain.luau");
    REQUIRE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // Headless (no data): tileId raises, so the run fails with a clear message instead of silently
    // producing an empty map — and nothing was painted/placed. CI-checks the script compiles and
    // that the error surfaces; the live fill/scatter runs against real data (GUI / gecko-cli).
    const auto r = rt.run(source, api, fx.controller, "terrain");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("tiles.lst") != std::string::npos);
    CHECK(api.paintedTiles() == 0);
    CHECK(api.placedObjects() == 0);
}

TEST_CASE("The shipped scatter.luau requires a palette and tile", "[scripting][lua]") {
    std::ifstream file(std::string(GECK_SCRIPTS_DIR) + "/editor/scatter.luau");
    REQUIRE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    // No --arg tile: the generic generator fails fast with a clear message (it compiles and its
    // required-arg guard fires) rather than producing an empty map.
    const auto r = rt.run(source, api, fx.controller, "scatter");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("tile") != std::string::npos);
    CHECK(api.paintedTiles() == 0);
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

TEST_CASE("LuaSandboxHost can repoint captured print output", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaSandboxHost host;
    std::string first;
    std::string second;
    std::string error;

    REQUIRE(host.initialize(api, {}, first, error));
    REQUIRE(host.loadSource("print('first')", error));
    REQUIRE(host.runLoaded(error));
    CHECK(first == "first\n");
    CHECK(second.empty());

    host.setPrintOutput(second);
    REQUIRE(host.loadSource("print('second')", error));
    REQUIRE(host.runLoaded(error));
    CHECK(first == "first\n");
    CHECK(second == "second\n");
}

TEST_CASE("LuaSandboxHost refuses to run before it is initialized", "[scripting][lua]") {
    LuaSandboxHost host;
    std::string error;

    CHECK_FALSE(host.loadSource("print('x')", error));
    CHECK(error == "sandbox not initialized");
    CHECK_FALSE(host.runLoaded(error));
    CHECK(error == "sandbox not initialized");
}

TEST_CASE("LuaSandboxHost refuses to run with no chunk loaded", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaSandboxHost host;
    std::string out;
    std::string error;
    REQUIRE(host.initialize(api, {}, out, error));

    // Must not lua_pcall whatever happens to be on the stack.
    CHECK_FALSE(host.runLoaded(error));
    CHECK(error == "no script loaded");

    // ...and the host is still usable afterwards.
    REQUIRE(host.loadSource("print('ok')", error));
    REQUIRE(host.runLoaded(error));
    CHECK(out == "ok\n");
}

TEST_CASE("LuaSandboxHost discards a chunk that was loaded but never run", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaSandboxHost host;
    std::string out;
    std::string error;
    REQUIRE(host.initialize(api, {}, out, error));

    // A second load supersedes the first: the abandoned chunk neither runs nor stays on the stack.
    REQUIRE(host.loadSource("print('abandoned')", error));
    REQUIRE(host.loadSource("print('latest')", error));
    REQUIRE(host.runLoaded(error));
    CHECK(out == "latest\n");

    // A failed compile also clears the pending chunk rather than leaving it runnable.
    REQUIRE(host.loadSource("print('pending')", error));
    CHECK_FALSE(host.loadSource("this is not lua", error));
    CHECK(error.find("compile error") != std::string::npos);
    CHECK_FALSE(host.runLoaded(error));
    CHECK(error == "no script loaded");
    CHECK(out == "latest\n");
}

TEST_CASE("LuaSandboxHost enforces its own time budget and stays usable", "[scripting][lua][watchdog]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaSandboxHost host;
    std::string out;
    std::string error;
    REQUIRE(host.initialize(api, {}, out, error));

    REQUIRE(host.loadSource("while true do end", error));
    CHECK_FALSE(host.runLoaded(error, /*timeBudgetMs*/ 50));
    CHECK(error.find("time budget") != std::string::npos);

    // The timeout is per-run, not sticky across runs: the same host runs the next chunk normally.
    REQUIRE(host.loadSource("print('after')", error));
    REQUIRE(host.runLoaded(error));
    CHECK(out == "after\n");
}

// Every function in the script_api reference (scriptApiEntries) must actually be bound on `api`, so
// the agent-facing reference can't claim a function the runtime doesn't provide. (LuaBridge hides the
// metatable under the Luau sandbox, so we can't enumerate the reverse direction in Lua; the
// kScriptApiEntries comment keeps additions documented, and this catches a stale/renamed entry.)
TEST_CASE("every documented script_api function is bound", "[scripting][lua]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV);
    LuaScriptRuntime rt;

    std::ostringstream script;
    script << "local missing = {}\n";
    for (const ScriptApiEntry& entry : scriptApiEntries()) {
        script << "if type(api." << entry.name << ") ~= 'function' then missing[#missing + 1] = '"
               << entry.name << "' end\n";
    }
    script << "print(table.concat(missing, ','))\n";

    const auto r = rt.run(script.str(), api, fx.controller, "check-api");
    INFO("script error: " << r.error);
    REQUIRE(r.ok);
    INFO("documented but not bound: " << r.output);
    CHECK(r.output == "\n"); // print() of an empty join is just a newline — nothing missing
}
