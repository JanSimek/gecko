#include "scripting/LuaSandboxHost.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <random>

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <LuaBridge/LuaBridge.h>
#include <LuaBridge/Map.h>    // std::map <-> Lua table (for mapSceneryHistogram)
#include <LuaBridge/Vector.h> // std::vector <-> Lua table (for hexNeighbors)

#include "scripting/MapScriptApi.h"
#include "scripting/ScriptApiReference.h"

namespace geck {

namespace {
    // Raised by the watchdog and reported by runLoaded(); kept in one place so the two agree.
    constexpr const char* TIME_BUDGET_ERROR = "script exceeded its time budget (possible infinite loop)";
}

LuaSandboxHost::~LuaSandboxHost() {
    close();
}

void* LuaSandboxHost::trackingAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* self = static_cast<LuaSandboxHost*>(ud);
    const size_t oldSize = ptr != nullptr ? osize : 0;
    if (nsize == 0) {
        self->_memoryUsed -= oldSize;
        std::free(ptr);
        return nullptr;
    }
    // Refuse only GROWTH past the limit — shrinks and frees must always succeed, or the GC
    // could not reclaim its way back under the cap. Luau surfaces the nullptr as a
    // "not enough memory" script error; the state itself stays alive.
    if (self->_memoryLimit != 0 && nsize > oldSize
        && self->_memoryUsed + (nsize - oldSize) > self->_memoryLimit) {
        return nullptr;
    }
    void* grown = std::realloc(ptr, nsize);
    if (grown != nullptr) {
        self->_memoryUsed = self->_memoryUsed - oldSize + nsize;
    }
    return grown;
}

bool LuaSandboxHost::initialize(MapScriptApi& api, const ScriptArgs& args, std::string& output,
    std::string& error) {
    return initialize(api, args, output, error, Options{});
}

bool LuaSandboxHost::initialize(MapScriptApi& api, const ScriptArgs& args, std::string& output,
    std::string& error, const Options& options) {
    close();
    error.clear();
    _output = &output;
    _memoryLimit = options.memoryLimitBytes;
    _memoryUsed = 0;
    // One allocator path for both modes: with no limit the tracker just counts.
    _state = lua_newstate(&LuaSandboxHost::trackingAlloc, this);
    if (_state == nullptr) {
        error = "failed to create Luau state";
        return false;
    }

    luaL_openlibs(_state); // Luau's stdlib is already safe: no `io`, `os` trimmed, no bytecode loaders

    // Replace print() with one that calls back into this host. The output string can be repointed
    // later without rebinding the global, which is useful for future resident plugin VMs.
    lua_pushlightuserdata(_state, this);
    lua_pushcclosure(_state, &LuaSandboxHost::capturePrint, "print", 1);
    lua_setglobal(_state, "print");

    // Bind the host API before luaL_sandbox(), which makes globals read-only. The functions come
    // from the single GECK_SCRIPT_API list that also drives the script_api reference.
#define GECK_SCRIPT_API_BIND(name, sig, doc) .addFunction(#name, &MapScriptApi::name)
    luabridge::getGlobalNamespace(_state)
        .beginClass<MapScriptApi>("MapScriptApi")
            GECK_SCRIPT_API(GECK_SCRIPT_API_BIND)
        .endClass();
#undef GECK_SCRIPT_API_BIND
    luabridge::setGlobal(_state, &api, "api");

    // Resolve this run's RNG seed: use --arg seed=N when it parses, otherwise pick a fresh one so
    // each per-run host differs. Luau's math.randomseed truncates to a 32-bit signed int, so keep
    // the seed in that range; the counter avoids collisions if random_device repeats.
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
                fromArg = false;
            }
        }
        if (!fromArg) {
            std::random_device rd;
            seed = static_cast<int>((rd() ^ runCounter.fetch_add(1)) & 0x7FFFFFFF);
        }
    }

    // Seed the api's deterministic stream (api:rng()/rngInt()) from the same resolved seed, so both
    // RNGs reproduce from --arg seed=N in every host (console, CLI, MCP). A caller that pre-seeds the
    // api (the fill preview) passes the identical seed in `args`, so this re-seed is idempotent there.
    api.setSeed(static_cast<uint32_t>(seed));

    // Expose caller parameters as the global table `args`, and always publish the resolved seed so
    // a script can report it ("re-run with --arg seed=N").
    {
        auto argsTable = luabridge::newTable(_state);
        for (const auto& [key, value] : args) {
            argsTable[key] = value;
        }
        argsTable["seed"] = std::to_string(seed);
        luabridge::setGlobal(_state, argsTable, "args");
    }

    luaL_sandbox(_state);

    if (options.persistentEnv) {
        // The resident-plugin model (Luau's recommended sandboxing shape): the shared globals
        // above are frozen readonly, and chunks run on a thread whose environment is a private
        // WRITABLE proxy over them — so a plugin's global writes persist across dispatches
        // without ever touching the shared table. The thread stays anchored on the main
        // state's stack so the GC cannot collect it.
        _thread = lua_newthread(_state);
        luaL_sandboxthread(_thread);
    }

    // Seed math.random from the resolved seed, so the run is reproducible. A script may still call
    // math.randomseed itself to override.
    lua_getglobal(_state, "math");          // [math]
    lua_getfield(_state, -1, "randomseed"); // [math, randomseed]
    lua_pushinteger(_state, seed);
    if (lua_pcall(_state, 1, 0, 0) != 0) { // success: [math]; error: [math, err]
        lua_pop(_state, 1);                // discard error; seeding is best-effort
    }
    lua_pop(_state, 1); // pop math

    return true;
}

void LuaSandboxHost::setPrintOutput(std::string& output) {
    _output = &output;
}

bool LuaSandboxHost::loadSource(const std::string& source, std::string& error) {
    error.clear();
    if (_state == nullptr) {
        _hasLoadedChunk = false;
        error = "sandbox not initialized";
        return false;
    }

    lua_State* exec = execState();
    // Drop a chunk an earlier loadSource() pushed but nobody ran. runLoaded()'s lua_pcall is what
    // normally pops it, so an abandoned load would otherwise sit on the stack for the life of the
    // state, keeping its prototype reachable — a slow leak in a resident VM that loads per dispatch.
    if (_hasLoadedChunk) {
        lua_pop(exec, 1);
        _hasLoadedChunk = false;
    }

    // Luau has no source loader: compile to bytecode, then load it. luau_compile mallocs the
    // buffer, so own it with a free-deleter rather than a manual free. Loading on execState()
    // gives the chunk the persistent thread env when one is enabled.
    size_t bytecodeSize = 0;
    const std::unique_ptr<char, void (*)(void*)> bytecode(
        luau_compile(source.data(), source.size(), nullptr, &bytecodeSize), std::free);
    const int loadResult = luau_load(exec, "=script", bytecode.get(), bytecodeSize, 0);
    if (loadResult != 0) {
        const char* raised = lua_tostring(exec, -1);
        error = std::string("compile error: ") + (raised != nullptr ? raised : "unknown");
        lua_pop(exec, 1);
        return false;
    }

    _hasLoadedChunk = true;
    return true;
}

bool LuaSandboxHost::runLoaded(std::string& error, unsigned timeBudgetMs) {
    error.clear();
    if (_state == nullptr) {
        error = "sandbox not initialized";
        return false;
    }
    if (!_hasLoadedChunk) {
        error = "no script loaded";
        return false;
    }

    _timedOut = false;
    if (timeBudgetMs > 0) {
        _deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeBudgetMs);
        lua_callbacks(_state)->userdata = this;
        lua_callbacks(_state)->interrupt = &LuaSandboxHost::timeBudgetInterrupt;
    }

    lua_State* exec = execState();
    const int result = lua_pcall(exec, 0, 0, 0);
    _hasLoadedChunk = false;
    const bool timedOut = _timedOut;
    disarmInterrupt();

    if (result != 0) {
        // lua_tostring yields nullptr for a non-string error object (`error({})`), so never feed it
        // straight to std::string.
        const char* raised = lua_tostring(exec, -1);
        std::string message = raised != nullptr ? raised : "non-string error object";
        lua_pop(exec, 1);
        // The budget is the host's verdict, not the script's: if the run timed out, say so even when
        // the script caught the watchdog error and raised something else in its place.
        if (timedOut && message.find(TIME_BUDGET_ERROR) == std::string::npos) {
            message = TIME_BUDGET_ERROR;
        }
        error = "runtime error: " + message;
        return false;
    }

    if (timedOut) {
        // The chunk swallowed the watchdog error and still returned cleanly. It ran over budget.
        error = std::string("runtime error: ") + TIME_BUDGET_ERROR;
        return false;
    }

    return true;
}

int LuaSandboxHost::capturePrint(lua_State* L) {
    auto* host = static_cast<LuaSandboxHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (host == nullptr || host->_output == nullptr) {
        return 0;
    }

    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) {
            host->_output->push_back('\t');
        }
        host->appendPrintValue(L, i);
    }
    host->_output->push_back('\n');
    return 0;
}

// Called by Luau at safepoints (call, return, and every loop back-edge) and by the collector.
// `gc >= 0` marks a GC step: we must not longjmp out of the collector, so only the non-GC
// safepoints can abort.
void LuaSandboxHost::timeBudgetInterrupt(lua_State* L, int gc) {
    if (gc >= 0) {
        return;
    }

    auto* host = static_cast<LuaSandboxHost*>(lua_callbacks(L)->userdata);
    if (host == nullptr) {
        return;
    }
    // Sticky: once expired, stay expired. Skips a clock read on every later safepoint too.
    if (!host->_timedOut && std::chrono::steady_clock::now() < host->_deadline) {
        return;
    }
    host->_timedOut = true;

    // Deliberately stay armed. Disarming here would let a script-level `pcall` catch this error and
    // then run unwatched forever. Armed, every subsequent safepoint raises again, and since Luau
    // puts a safepoint on call/return/back-edge the chunk cannot make progress or even return — the
    // error walks out to our lua_pcall. Re-raising inside an xpcall handler is bounded too: Luau
    // runs the handler under luaD_rawrunprotected and degrades to "error in error handling".
    luaL_error(L, "%s", TIME_BUDGET_ERROR);
}

void LuaSandboxHost::disarmInterrupt() noexcept {
    if (_state == nullptr) {
        return;
    }
    lua_callbacks(_state)->interrupt = nullptr;
    lua_callbacks(_state)->userdata = nullptr;
}

void LuaSandboxHost::close() noexcept {
    if (_state != nullptr) {
        // Disarm before closing: the interrupt must not fire (and longjmp) during lua_close's GC.
        disarmInterrupt();
        _thread = nullptr; // owned by the state; freed with it
        lua_close(_state);
        _state = nullptr;
    }
    _output = nullptr;
    _hasLoadedChunk = false;
    _timedOut = false;
    _memoryLimit = 0;
    _memoryUsed = 0;
}

void LuaSandboxHost::appendPrintValue(lua_State* L, int index) {
    if (_output == nullptr) {
        return;
    }
    size_t length = 0;
    const char* text = luaL_tolstring(L, index, &length); // converts any value; pushes the string
    _output->append(text, length);
    lua_pop(L, 1);
}

} // namespace geck
