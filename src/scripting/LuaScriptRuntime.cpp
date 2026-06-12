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

ScriptResult LuaScriptRuntime::run(const std::string& source, MapScriptApi& api,
    ObjectCommandController& controller, const std::string& description) {
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        return { false, "failed to create Luau state" };
    }
    luaL_openlibs(L); // Luau's stdlib is already safe: no `io`, `os` trimmed, no bytecode loaders

    // Bind the host API. Must precede luaL_sandbox(), which makes globals read-only.
    luabridge::getGlobalNamespace(L)
        .beginClass<MapScriptApi>("MapScriptApi")
        .addFunction("isValidHex", &MapScriptApi::isValidHex)
        .addFunction("hexNeighbors", &MapScriptApi::hexNeighbors)
        .addFunction("getFloor", &MapScriptApi::getFloor)
        .addFunction("getRoof", &MapScriptApi::getRoof)
        .addFunction("placeObject", &MapScriptApi::placeObject)
        .addFunction("paintFloor", &MapScriptApi::paintFloor)
        .addFunction("paintRoof", &MapScriptApi::paintRoof)
        .endClass();
    luabridge::setGlobal(L, &api, "api");

    luaL_sandbox(L);

    // Luau has no source loader: compile to bytecode, then load it. luau_compile mallocs
    // the buffer, so own it with a free-deleter rather than a manual free.
    size_t bytecodeSize = 0;
    const std::unique_ptr<char, void (*)(void*)> bytecode(
        luau_compile(source.data(), source.size(), nullptr, &bytecodeSize), std::free);
    const int loadResult = luau_load(L, "=script", bytecode.get(), bytecodeSize, 0);
    if (loadResult != 0) {
        ScriptResult result{ false, std::string("compile error: ") + lua_tostring(L, -1) };
        lua_close(L);
        return result;
    }

    ScriptResult result;
    {
        // The whole run is one undo entry: every api mutator buffers into this batch, and
        // endBatch() (on scope exit) collapses them — even if the script errors part-way.
        ScopedUndoBatch batch(controller, description);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            result = { false, std::string("runtime error: ") + lua_tostring(L, -1) };
        } else {
            result = { true, "" };
        }
    }

    lua_close(L);
    return result;
}

} // namespace geck
