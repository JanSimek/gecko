#include "scripting/LuaScriptRuntime.h"

#include <cstdlib>
#include <memory>

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <LuaBridge/LuaBridge.h>
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
        .addFunction("placeObject", &MapScriptApi::placeObject)
        .addFunction("placeProto", &MapScriptApi::placeProto)
        .addFunction("paintFloor", &MapScriptApi::paintFloor)
        .addFunction("paintRoof", &MapScriptApi::paintRoof)
        .endClass();
    luabridge::setGlobal(L, &api, "api");

    // Expose caller parameters as the global table `args` (string -> string). Must precede
    // luaL_sandbox(), which makes globals read-only.
    {
        auto argsTable = luabridge::newTable(L);
        for (const auto& [key, value] : args) {
            argsTable[key] = value;
        }
        luabridge::setGlobal(L, argsTable, "args");
    }

    luaL_sandbox(L);

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
