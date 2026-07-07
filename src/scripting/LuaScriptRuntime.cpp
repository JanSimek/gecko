#include "scripting/LuaScriptRuntime.h"

#include <atomic>
#include <chrono>
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
#include "scripting/ScriptApiReference.h"
#include "editing/commands/ObjectCommandController.h"

namespace geck {

namespace {
    // Time-budget watchdog. A pointer to one of these lives in lua_callbacks(L)->userdata (the
    // documented per-state scratch slot, untouched by Luau and unused by LuaBridge), so the
    // safepoint interrupt below can read the deadline straight off the lua_State.
    struct TimeBudget {
        std::chrono::steady_clock::time_point deadline;
    };

    // Called by Luau at safepoints (loop back-edges, call/ret, gc). `gc >= 0` marks a GC step:
    // we must not longjmp out of the collector, so we only abort on the non-GC safepoints. Past
    // the deadline, luaL_error raises a runtime error that unwinds the pcall — the standard Luau
    // timeout pattern. Clearing the callback first stops it re-firing during the unwind.
    void timeBudgetInterrupt(lua_State* L, int gc) {
        if (gc >= 0) {
            return;
        }
        const auto* budget = static_cast<const TimeBudget*>(lua_callbacks(L)->userdata);
        if (budget == nullptr) {
            return;
        }
        if (std::chrono::steady_clock::now() >= budget->deadline) {
            lua_callbacks(L)->interrupt = nullptr;
            luaL_error(L, "script exceeded its time budget (possible infinite loop)");
        }
    }

    // RAII teardown for the run's lua_State: on EVERY exit path (normal return, early compile-error
    // return, or a C++ exception unwinding out of the run) it disarms the watchdog FIRST — so the
    // interrupt can't fire (and luaL_error/longjmp) during lua_close's GC, or read the by-then-dead
    // TimeBudget — then closes the state so it never leaks. Disarming is two pointer writes and
    // lua_close runs no Lua callbacks once disarmed, so the destructor never throws.
    struct LuaStateGuard {
        lua_State* L;
        ~LuaStateGuard() {
            if (L == nullptr) {
                return;
            }
            lua_callbacks(L)->interrupt = nullptr;
            lua_callbacks(L)->userdata = nullptr;
            lua_close(L);
        }
    };

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
    ObjectCommandController& controller, const std::string& description, const ScriptArgs& args,
    unsigned timeBudgetMs) {
    ScriptResult result;

    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        return { false, "failed to create Luau state", "" };
    }
    // From here, the state (and the watchdog disarm) is owned by RAII: every return below, and any
    // exception unwinding through this function, tears it down correctly.
    LuaStateGuard stateGuard{ L };
    luaL_openlibs(L); // Luau's stdlib is already safe: no `io`, `os` trimmed, no bytecode loaders

    // Replace print() with one that captures into result.output (a stable stack local for this run).
    // Must precede luaL_sandbox(), which makes globals read-only.
    lua_pushlightuserdata(L, &result.output);
    lua_pushcclosure(L, &capturePrint, "print", 1);
    lua_setglobal(L, "print");

    // Bind the host API. Must precede luaL_sandbox(), which makes globals read-only. The functions
    // come from the single GECK_SCRIPT_API list (ScriptApiReference.h) that also drives the
    // script_api reference, so bindings and docs cannot diverge.
#define GECK_SCRIPT_API_BIND(name, sig, doc) .addFunction(#name, &MapScriptApi::name)
    luabridge::getGlobalNamespace(L)
        .beginClass<MapScriptApi>("MapScriptApi")
            GECK_SCRIPT_API(GECK_SCRIPT_API_BIND)
        .endClass();
#undef GECK_SCRIPT_API_BIND
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

    // Seed the api's deterministic stream (api:rng()/rngInt()) from the same resolved seed, so
    // both RNGs reproduce from --arg seed=N in every host (console, CLI, MCP). A host that
    // pre-seeds the api (the fill preview) passes the identical seed in `args`, so this re-seed
    // is idempotent there.
    api.setSeed(static_cast<uint32_t>(seed));

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
        return result; // stateGuard closes L
    }

    // Arm the time-budget watchdog (GUI live previews pass a budget; trusted CLI/batch runs pass 0).
    // The TimeBudget lives on this stack frame, which outlives the pcall below; its address goes in
    // the per-state userdata slot the interrupt reads. The deadline is taken here so it covers only
    // execution, not the compile above.
    TimeBudget budget;
    if (timeBudgetMs > 0) {
        budget.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeBudgetMs);
        lua_callbacks(L)->userdata = &budget;
        lua_callbacks(L)->interrupt = &timeBudgetInterrupt;
    }

    {
        // The whole run is one undo entry: a committing run's api mutators buffer into this batch and
        // endBatch() (on scope exit) collapses them — even if the script errors part-way. When the
        // caller has installed a plan sink (a fill preview), the mutators record into the sink and
        // commit nothing, so this batch stays empty and endBatch() pushes nothing — a harmless no-op.
        ScopedUndoBatch batch(controller, description);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            result.ok = false;
            result.error = std::string("runtime error: ") + lua_tostring(L, -1);
        } else {
            result.ok = true;
        }
    }

    // stateGuard disarms the watchdog and closes the state on return (and on any exception above).
    return result;
}

} // namespace geck
