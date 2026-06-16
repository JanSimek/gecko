#include "scripting/LuaScriptRuntime.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <LuaBridge/LuaBridge.h>
#include <LuaBridge/Map.h>    // std::map <-> Lua table (for mapSceneryHistogram)
#include <LuaBridge/Vector.h> // std::vector <-> Lua table (for hexNeighbors)

#include "scripting/MapScriptApi.h"
#include "ui/editing/ObjectCommandController.h"

namespace geck {

namespace {
    // print(...) capture: append each argument (tab-separated) plus a newline to the std::string
    // carried as a lightuserdata upvalue, so console scripts can report progress.
    int capturePrint(lua_State* L) {
        auto* out = static_cast<std::string*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (out == nullptr) {
            return 0;
        }
        const int count = lua_gettop(L);
        for (int i = 1; i <= count; ++i) {
            if (i > 1) {
                out->push_back('\t');
            }
            size_t length = 0;
            const char* text = luaL_tolstring(L, i, &length); // converts any value; pushes the string
            out->append(text, length);
            lua_pop(L, 1);
        }
        out->push_back('\n');
        return 0;
    }
}

ScriptResult LuaScriptRuntime::run(const std::string& source, MapScriptApi& api,
    ObjectCommandController& controller, const std::string& description, const ScriptArgs& args) {
    ScriptResult result;

    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        return { false, "failed to create Luau state", "" };
    }
    luaL_openlibs(L); // Luau's stdlib is already safe: no `io`, `os` trimmed, no bytecode loaders

    // Replace print() with one that captures into result.output (a stable stack local for this run).
    // Must precede luaL_sandbox(), which makes globals read-only.
    lua_pushlightuserdata(L, &result.output);
    lua_pushcclosure(L, &capturePrint, "print", 1);
    lua_setglobal(L, "print");

    // Bind the host API. Must precede luaL_sandbox(), which makes globals read-only.
    luabridge::getGlobalNamespace(L)
        .beginClass<MapScriptApi>("MapScriptApi")
        .addFunction("isValidHex", &MapScriptApi::isValidHex)
        .addFunction("hexNeighbors", &MapScriptApi::hexNeighbors)
        .addFunction("getFloor", &MapScriptApi::getFloor)
        .addFunction("getRoof", &MapScriptApi::getRoof)
        .addFunction("tileId", &MapScriptApi::tileId)
        .addFunction("mapScenery", &MapScriptApi::mapScenery)
        .addFunction("mapSceneryHistogram", &MapScriptApi::mapSceneryHistogram)
        .addFunction("mapFloorTiles", &MapScriptApi::mapFloorTiles)
        .addFunction("listMaps", &MapScriptApi::listMaps)
        .addFunction("noise2d", &MapScriptApi::noise2d)
        .addFunction("protoName", &MapScriptApi::protoName)
        .addFunction("proto", &MapScriptApi::proto)
        .addFunction("hexIndex", &MapScriptApi::hexIndex)
        .addFunction("tileIndex", &MapScriptApi::tileIndex)
        .addFunction("hexCol", &MapScriptApi::hexCol)
        .addFunction("hexRow", &MapScriptApi::hexRow)
        .addFunction("tileCol", &MapScriptApi::tileCol)
        .addFunction("tileRow", &MapScriptApi::tileRow)
        .addFunction("getFloorXY", &MapScriptApi::getFloorXY)
        .addFunction("getRoofXY", &MapScriptApi::getRoofXY)
        .addFunction("placeObjectXY", &MapScriptApi::placeObjectXY)
        .addFunction("placeProtoXY", &MapScriptApi::placeProtoXY)
        .addFunction("paintFloorXY", &MapScriptApi::paintFloorXY)
        .addFunction("paintRoofXY", &MapScriptApi::paintRoofXY)
        .addFunction("placeObject", &MapScriptApi::placeObject)
        .addFunction("placeProto", &MapScriptApi::placeProto)
        .addFunction("paintFloor", &MapScriptApi::paintFloor)
        .addFunction("paintRoof", &MapScriptApi::paintRoof)
        .endClass();
    luabridge::setGlobal(L, &api, "api");

    // Resolve this run's RNG seed: use --arg seed=N when it parses, otherwise pick a fresh one so
    // each run differs (every run() builds a new lua_State, so re-running a generator in the Script
    // Console gives a new layout). Luau's math.randomseed truncates to a 32-bit *signed* int, so the
    // seed is kept in that range; the random path mixes in a per-process counter so two runs never
    // collide even if random_device repeats.
    int seed = 0;
    {
        static std::atomic<uint32_t> runCounter{ 0 };
        const auto it = args.find("seed");
        bool fromArg = false;
        if (it != args.end()) {
            try {
                seed = static_cast<int>(std::stol(it->second));
                fromArg = true;
            } catch (const std::exception&) {
                fromArg = false; // not a number -> fall through to a random seed
            }
        }
        if (!fromArg) {
            std::random_device rd;
            seed = static_cast<int>((rd() ^ runCounter.fetch_add(1)) & 0x7FFFFFFF);
        }
    }

    // Expose caller parameters as the global table `args` (string -> string), and always publish
    // the resolved seed as args.seed so a script can report it ("re-run with --arg seed=N"). Must
    // precede luaL_sandbox(), which makes globals read-only.
    {
        auto argsTable = luabridge::newTable(L);
        for (const auto& [key, value] : args) {
            argsTable[key] = value;
        }
        argsTable["seed"] = std::to_string(seed);
        luabridge::setGlobal(L, argsTable, "args");
    }

    luaL_sandbox(L);

    // Seed math.random from the resolved seed, so the run is reproducible: the same --arg seed
    // gives the same layout. (A script may still call math.randomseed itself to override.)
    {
        lua_getglobal(L, "math");          // [math]
        lua_getfield(L, -1, "randomseed"); // [math, randomseed]
        lua_pushinteger(L, seed);
        if (lua_pcall(L, 1, 0, 0) != 0) { // success: [math]; error: [math, err]
            lua_pop(L, 1);                // discard error; seeding is best-effort
        }
        lua_pop(L, 1); // pop math
    }

    // Luau has no source loader: compile to bytecode, then load it. luau_compile mallocs
    // the buffer, so own it with a free-deleter rather than a manual free.
    size_t bytecodeSize = 0;
    const std::unique_ptr<char, void (*)(void*)> bytecode(
        luau_compile(source.data(), source.size(), nullptr, &bytecodeSize), std::free);
    const int loadResult = luau_load(L, "=script", bytecode.get(), bytecodeSize, 0);
    if (loadResult != 0) {
        result.ok = false;
        result.error = std::string("compile error: ") + lua_tostring(L, -1);
        lua_close(L);
        return result;
    }

    {
        // The whole run is one undo entry: every api mutator buffers into this batch, and
        // endBatch() (on scope exit) collapses them — even if the script errors part-way.
        ScopedUndoBatch batch(controller, description);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            result.ok = false;
            result.error = std::string("runtime error: ") + lua_tostring(L, -1);
        } else {
            result.ok = true;
        }
    }

    lua_close(L);
    return result;
}

} // namespace geck
